#include "ma_library.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace esphome::ma_library {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static const char *const ASK = "call_service:music_assistant.get_library";
static const char *const PLAY = "music_assistant.play_media";
// The most rows a page holds. An answer is at most about 3 KB and the app shortens a longer list from the end, so a page
// is asked in as many answers as it takes (LibraryTile::answer).
static constexpr int MAX_ROWS = 12;
// An answer that has not come after this long is taken as failed; a tap asks again.
static constexpr uint32_t ANSWER_MS = 15000;
// How long a tapped row says it is starting.
static constexpr uint32_t STARTING_MS = 3000;

// The three lists, in the order of the tabs: Music Assistant's media_type and the screen's word for it.
static const char *const KINDS[] = {"playlist", "artist", "radio"};
static const char *const KIND_WORDS[] = {"playlists", "artists", "radio"};
static constexpr int KIND_COUNT = 3;

// A kept text (in PSRAM) as a plain one, for a call that takes std::string.
static std::string plain(const tessera::Text &t) { return std::string(t.data(), t.size()); }

// One row: what Music Assistant's library calls it and the uri that plays it (library://playlist/12). The app cuts a
// text at 48 bytes.
struct Item {
  char name[52] = {};
  char uri[52] = {};
};

// The tile. Three tabs at the top (playlists, artists, radio), a page of names under them, and a row of keys at the
// bottom: back a page, the page number, on a page. A tap on a name plays it on the tile's entity, a Music Assistant
// player. Each page is asked of Home Assistant through the app (music_assistant.get_library), as many rows as fit.
class LibraryTile : public tessera::Tile {
 public:
  explicit LibraryTile(MaLibrary *plugin) : plugin_(plugin) { plugin_->tiles.push_back(this); }
  ~LibraryTile() override {
    auto &t = plugin_->tiles;
    t.erase(std::remove(t.begin(), t.end(), this), t.end());
  }

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    player_ = tessera::Text(c.entity ? c.entity : "");
    snprintf(entry_, sizeof(entry_), "%s", c.options["config_entry"] | "");
    const char *start = c.options["start"] | "playlist";
    for (int i = 0; i < KIND_COUNT; ++i) if (!strcmp(start, KINDS[i])) kind_ = i;

    // The tabs: a block each, a third of the width, the word in the middle. On a narrow tile one tab, the list shown,
    // which a tap turns to the next list.
    gap_ = ui::px(6);
    bar_h_ = ui::line_height(Font::BODY) + ui::px(14);
    tabs_ = (width_ - 2 * gap_) / KIND_COUNT >= ui::mm(14) ? KIND_COUNT : 1;
    const int tab_w = (width_ - (tabs_ - 1) * gap_) / tabs_;
    for (int i = 0; i < tabs_; ++i) {
      tab_[i] = key_block(c.parent, tab_w, bar_h_);
      lv_obj_set_pos(tab_[i], i * (tab_w + gap_), 0);
      tab_text_[i] = ui::label(tab_[i], Font::BODY, theme::INK);
      lv_obj_set_width(tab_text_[i], tab_w - ui::px(8));
      lv_obj_set_style_text_align(tab_text_[i], LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_center(tab_text_[i]);
      if (tabs_ == KIND_COUNT) ui::set_text(tab_text_[i], plugin_->text(KIND_WORDS[i]));
    }

    // The rows between the tabs and the keys, as many as fit, at most MAX_ROWS. The large font while it leaves three
    // rows at least, else the body font when that fits more on a short tile.
    rows_top_ = bar_h_ + gap_;
    const int rows_room = height_ - 2 * bar_h_ - 2 * gap_;
    const int pad = ui::px(ui::large() ? 14 : 8);
    const int large_rows = rows_room / (ui::line_height(Font::BODY_LARGE) + pad);
    Font row_font = Font::BODY_LARGE;
    if (large_rows < 3 && rows_room / (ui::line_height(Font::BODY) + pad) > large_rows) row_font = Font::BODY;
    row_h_ = ui::line_height(row_font) + pad;
    rows_ = std::max(1, std::min(MAX_ROWS, rows_room / row_h_));
    // The room left under the last row goes to the rows, so the list fills the tile whatever its size.
    row_h_ = std::max(row_h_, std::min(rows_room / rows_, row_h_ * 3 / 2));
    for (int i = 0; i < rows_; ++i) {
      row_[i] = ui::label(c.parent, row_font, theme::INK);
      lv_obj_set_width(row_[i], width_);
      lv_obj_set_pos(row_[i], 0, rows_top_ + i * row_h_ + (row_h_ - ui::line_height(row_font)) / 2);
    }

    // The keys at the bottom: back and on at the sides, the page number between them.
    keys_y_ = height_ - bar_h_;
    const int key_w = std::max(bar_h_, width_ / 4);
    for (int i = 0; i < 2; ++i) {
      key_[i] = key_block(c.parent, key_w, bar_h_);
      lv_obj_set_pos(key_[i], i == 0 ? 0 : width_ - key_w, keys_y_);
      key_text_[i] = ui::label(key_[i], Font::BODY_LARGE, theme::INK);
      lv_obj_set_width(key_text_[i], key_w);
      lv_obj_set_style_text_align(key_text_[i], LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_center(key_text_[i]);
    }
    // Arrows where the screen's font has them, else the plain signs.
    ui::set_text(key_text_[0], ui::covers("‹", Font::BODY_LARGE) ? "‹" : "<");
    ui::set_text(key_text_[1], ui::covers("›", Font::BODY_LARGE) ? "›" : ">");
    key_w_ = key_w;
    page_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(page_, width_ - 2 * key_w - 2 * gap_);
    lv_obj_set_style_text_align(page_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(page_, key_w + gap_, keys_y_ + (bar_h_ - ui::line_height(Font::BODY)) / 2);

    // What the tile says while it has no rows to show: in the middle of the rows' room.
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_size(note_, width_, std::max(1, std::min(rows_room, 3 * ui::line_height(Font::BODY))));
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note_, 0, rows_top_ + std::max(0, (rows_room - 2 * ui::line_height(Font::BODY)) / 2));

    ask();
    paint();
  }

  void on_state(JsonObjectConst data) override {
    // The player's own part: it comes with every change of the player. Nothing of it is drawn but whether there is one.
    if (!data["state"].isNull()) unavailable_ = strcmp(data["state"] | "", "unavailable") == 0;
    paint();
  }

  void on_tick(uint32_t) override {
    const uint32_t now = esphome::millis();
    // No app yet when the tile came on the glass: ask again every few seconds until the request goes out.
    if (state_ == State::UNSENT && now - asked_at_ >= 5000) ask();
    if (state_ == State::ASKING && now - asked_at_ >= ANSWER_MS) {
      give_up();
      paint();
    }
    if (starting_ >= 0 && now - started_at_ >= STARTING_MS) {
      starting_ = -1;
      paint();
    }
  }

  void on_theme() override {
    for (int i = 0; i < 2; ++i) ui::set_color(key_text_[i], theme::INK);
    ui::set_color(page_, theme::MUTED);
    ui::set_color(note_, theme::MUTED);
    paint();
  }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (p.x < 0 || p.y < 0) return;
    // A tab: another list, from its first page.
    if (p.y < bar_h_ + gap_ / 2) {
      const int k = tabs_ == 1 ? (kind_ + 1) % KIND_COUNT
                               : std::min(KIND_COUNT - 1, p.x * KIND_COUNT / std::max(1, width_));
      if (k != kind_ || state_ == State::FAILED) {
        kind_ = k;
        offset_ = 0;
        ask();
      }
      return paint();
    }
    // The keys: back or on a page.
    if (p.y >= keys_y_ - gap_ / 2) {
      if (p.x < key_w_ + gap_ && offset_ > 0) {
        offset_ = std::max(0, offset_ - rows_);
        ask();
      } else if (p.x >= width_ - key_w_ - gap_ && more_) {
        offset_ += fetched_;
        ask();
      }
      return paint();
    }
    // Nothing to play: a tap asks again after a failure.
    if (count_ == 0) {
      if (state_ == State::FAILED || state_ == State::UNSENT) {
        ask();
        paint();
      }
      return;
    }
    const int row = (p.y - rows_top_) / row_h_;
    if (row < 0 || row >= count_ || player_.empty()) return;
    JsonDocument d;
    d["media_id"] = items_[row].uri;
    d["media_type"] = KINDS[kind_];
    if (tessera::action(PLAY, plain(player_), d)) {
      starting_ = row;
      started_at_ = esphome::millis();
    }
    paint();
  }

  // An answer of the app, to any tile of the plugin: {"re", "ok", "result": {"items": [{"name", "uri", ...}, ...]}}.
  void answer(JsonObjectConst message) {
    if (state_ != State::ASKING || (message["re"] | 0u) != asked_) return;
    if (!(message["ok"] | false)) {
      give_up();
      return paint();
    }
    int got = 0;
    for (JsonObjectConst item : message["result"]["items"].as<JsonArrayConst>()) {
      if (count_ >= rows_) break;
      ++got;
      ++fetched_;
      const char *uri = item["uri"] | "";
      if (!uri[0]) continue;
      snprintf(items_[count_].name, sizeof(items_[count_].name), "%s", item["name"] | uri);
      snprintf(items_[count_].uri, sizeof(items_[count_].uri), "%s", uri);
      ++count_;
    }
    if (count_ >= rows_) {
      // A full page may have one after it.
      more_ = true;
      state_ = State::SHOWN;
    } else if (got > 0) {
      // Short: the list's end, or an answer the app shortened to fit. Only the next answer can tell, so ask on.
      request();
    } else if (count_ == 0 && offset_ > 0) {
      // Paged past the end (the library shrank): back to the first page.
      offset_ = 0;
      ask();
    } else {
      state_ = State::SHOWN;
    }
    paint();
  }

 private:
  enum class State : uint8_t { UNSENT, ASKING, SHOWN, FAILED };

  // Ask for the page at offset_ of the list of the tab that is chosen.
  void ask() {
    count_ = 0;
    fetched_ = 0;
    more_ = false;
    starting_ = -1;
    if (!entry_[0]) {
      asked_at_ = esphome::millis();
      state_ = State::FAILED;
      return;
    }
    request();
  }

  // Ask for what the page still lacks.
  void request() {
    asked_at_ = esphome::millis();
    JsonDocument request;
    request["ask"] = ASK;
    request["data"]["config_entry_id"] = entry_;
    request["data"]["media_type"] = KINDS[kind_];
    request["data"]["limit"] = rows_ - count_;
    request["data"]["offset"] = offset_ + fetched_;
    request["data"]["order_by"] = "name";
    asked_ = tessera::send(plugin_, request.as<JsonObjectConst>());
    state_ = asked_ ? State::ASKING : State::UNSENT;
  }

  // No answer. Rows that came stay, and the key goes on from the first row that did not.
  void give_up() {
    if (count_ > 0) {
      more_ = true;
      state_ = State::SHOWN;
    } else {
      state_ = State::FAILED;
    }
  }

  void paint() {
    if (tabs_ == 1) ui::set_text(tab_text_[0], plugin_->text(KIND_WORDS[kind_]));
    for (int i = 0; i < tabs_; ++i) {
      const bool on = tabs_ == 1 || i == kind_;
      ui::set_fill(tab_[i], on ? Tone::ACCENT : Tone::NORMAL, theme::TRACK);
      ui::set_color(tab_text_[i], on ? theme::ON_ACCENT : theme::INK);
    }
    // The note: why there is nothing to show, else nothing.
    const char *note = "";
    if (player_.empty()) note = "no_player";
    else if (!entry_[0]) note = "no_entry";
    else if (state_ == State::FAILED) note = "failed";
    else if (count_ == 0) note = state_ == State::SHOWN ? "none" : "waiting";
    ui::set_text(note_, note[0] ? plugin_->text(note) : "");
    const bool rows = !note[0];
    for (int i = 0; i < rows_; ++i) {
      const bool shown = rows && i < count_;
      ui::set_text(row_[i], !shown ? "" : i == starting_ ? std::string(plugin_->text("starting")) : items_[i].name);
      ui::set_tone(row_[i], i == starting_ ? Tone::ACCENT : Tone::NORMAL, unavailable_ ? theme::MUTED : theme::INK);
    }
    // The keys stand only where they do something; the page number only on a page.
    set_shown(key_[0], key_text_[0], rows && offset_ > 0);
    set_shown(key_[1], key_text_[1], rows && more_);
    ui::set_text(page_, rows ? tessera::format(plugin_->text("page"), offset_ / std::max(1, rows_) + 1) : "");
  }

  // A key: a block that holds its word, not clickable itself (the tile takes the tap).
  static lv_obj_t *key_block(lv_obj_t *parent, int w, int h) {
    lv_obj_t *o = ui::block(parent, theme::TRACK);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_size(o, w, h);
    return o;
  }

  static void set_shown(lv_obj_t *block, lv_obj_t *label, bool shown) {
    if (shown) {
      lv_obj_clear_flag(block, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(block, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  MaLibrary *plugin_;
  tessera::Text player_;
  char entry_[72] = {};
  int tabs_ = KIND_COUNT, kind_ = 0, offset_ = 0, count_ = 0, fetched_ = 0, rows_ = 1, starting_ = -1;
  bool more_ = false, unavailable_ = false;
  State state_ = State::UNSENT;
  uint32_t asked_ = 0, asked_at_ = 0, started_at_ = 0;
  int width_ = 0, height_ = 0, gap_ = 0, bar_h_ = 0, row_h_ = 0, rows_top_ = 0, keys_y_ = 0, key_w_ = 0;
  Item items_[MAX_ROWS];
  lv_obj_t *tab_[KIND_COUNT]{}, *tab_text_[KIND_COUNT]{}, *row_[MAX_ROWS]{}, *key_[2]{}, *key_text_[2]{};
  lv_obj_t *page_{}, *note_{};
};

void MaLibrary::setup() {
  add_tile("library", [this]() { return new LibraryTile(this); });
}

void MaLibrary::on_message(JsonObjectConst message) {
  for (LibraryTile *tile : tiles) tile->answer(message);
}

}  // namespace esphome::ma_library
