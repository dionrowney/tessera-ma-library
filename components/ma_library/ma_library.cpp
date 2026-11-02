#include "ma_library.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace esphome::ma_library {

using tessera::Font;
namespace ui = tessera::ui;

static const char *const ASK = "call_service:music_assistant.get_library";
static const char *const PLAY = "music_assistant.play_media";
// The most rows a page holds. An answer is at most about 3 KB and the app shortens a longer list from the end, so a page
// is asked in as many answers as it takes (ListPopup::answer).
static constexpr int MAX_ROWS = 12;
// An answer that has not come after this long is taken as failed; a tap asks again.
static constexpr uint32_t ANSWER_MS = 15000;

// The lists a tile can open: Music Assistant's media_type and the screen's word for it. A new list is a line here, a
// choice of the option `list` in the manifest, and its word in translations/en.json (parts `screen` and `app`).
struct Kind {
  const char *type;
  const char *word;
};
static const Kind KINDS[] = {
    {"playlist", "playlists"},
    {"artist", "artists"},
    {"radio", "radio"},
    {"podcast", "podcasts"},
};

// One row: what Music Assistant's library calls it and the uri that plays it (library://playlist/12). The app cuts a
// text at 48 bytes.
struct Item {
  char name[52] = {};
  char uri[52] = {};
};

// What a tile hands the popup it opens: copies, since the popup may outlive the tile's page.
struct ListRequest {
  char title[52] = {};
  char player[72] = {};
  int kind = 0;
};

// A key: a block that holds its word, not clickable itself (the popup takes the tap).
static lv_obj_t *key_block(lv_obj_t *parent, int w, int h) {
  lv_obj_t *o = ui::block(parent, theme::TRACK);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_size(o, w, h);
  return o;
}

// The list, over the whole glass: its title beside the close key, a page of names, and a row of keys at the bottom. A
// tap on a name plays it on the tile's player and closes the popup. Each page is asked of Home Assistant through the app
// (music_assistant.get_library), as many rows as fit.
class ListPopup : public tessera::Popup {
 public:
  ListPopup(MaLibrary *plugin, const ListRequest &request) : plugin_(plugin), request_(request) {}

  void open(const tessera::PopupContext &c) override {
    width_ = c.width;
    gap_ = ui::px(6);
    bar_h_ = ui::line_height(Font::BODY) + ui::px(14);

    // The first row: the title, leaving the corner to the close key.
    const int head = std::max(c.key, ui::line_height(Font::TITLE));
    title_ = ui::label(c.parent, Font::TITLE, theme::INK);
    lv_obj_set_width(title_, c.width - c.corner);
    lv_obj_set_pos(title_, 0, (head - ui::line_height(Font::TITLE)) / 2);
    ui::set_text(title_, request_.title);

    // The rows between the title and the keys, as many as fit, and the room left under the last goes to the rows.
    rows_top_ = head + gap_;
    keys_y_ = c.height - bar_h_;
    const int rows_room = keys_y_ - gap_ - rows_top_;
    const int line = ui::line_height(Font::BODY_LARGE);
    row_h_ = line + ui::px(ui::large() ? 14 : 8);
    rows_ = std::max(1, std::min(MAX_ROWS, rows_room / row_h_));
    row_h_ = std::max(row_h_, std::min(rows_room / rows_, row_h_ * 3 / 2));
    for (int i = 0; i < rows_; ++i) {
      row_[i] = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
      lv_obj_set_width(row_[i], width_);
      lv_obj_set_pos(row_[i], 0, rows_top_ + i * row_h_ + (row_h_ - line) / 2);
    }

    // The card is as high as what is in it: a part that never hides holds it at its full height, whatever shows.
    lv_obj_t *floor = key_block(c.parent, 1, bar_h_);
    lv_obj_set_style_bg_opa(floor, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(floor, width_ / 2, keys_y_);

    // The keys at the bottom: back and on at the sides, the page number between them.
    key_w_ = std::max(bar_h_, width_ / 4);
    for (int i = 0; i < 2; ++i) {
      key_[i] = key_block(c.parent, key_w_, bar_h_);
      lv_obj_set_pos(key_[i], i == 0 ? 0 : width_ - key_w_, keys_y_);
      key_text_[i] = ui::label(key_[i], Font::BODY_LARGE, theme::INK);
      lv_obj_set_width(key_text_[i], key_w_);
      lv_obj_set_style_text_align(key_text_[i], LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_center(key_text_[i]);
    }
    // Arrows where the screen's font has them, else the plain signs.
    ui::set_text(key_text_[0], ui::covers("‹", Font::BODY_LARGE) ? "‹" : "<");
    ui::set_text(key_text_[1], ui::covers("›", Font::BODY_LARGE) ? "›" : ">");
    page_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(page_, width_ - 2 * key_w_ - 2 * gap_);
    lv_obj_set_style_text_align(page_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(page_, key_w_ + gap_, keys_y_ + (bar_h_ - ui::line_height(Font::BODY)) / 2);

    // What it says while it has no rows to show: in the middle of the rows' room.
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_size(note_, width_, std::max(1, std::min(rows_room, 3 * ui::line_height(Font::BODY))));
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note_, 0, rows_top_ + std::max(0, (rows_room - 2 * ui::line_height(Font::BODY)) / 2));

    ask();
    paint();
  }

  void on_tick(uint32_t) override {
    const uint32_t now = esphome::millis();
    // No connection to the app when it was asked: ask again every few seconds until the request goes out.
    if (state_ == State::UNSENT && now - asked_at_ >= 5000) {
      ask();
      paint();
    }
    if (state_ == State::ASKING && now - asked_at_ >= ANSWER_MS) {
      give_up();
      paint();
    }
  }

  void on_theme() override {
    ui::set_color(title_, theme::INK);
    for (int i = 0; i < rows_; ++i) ui::set_color(row_[i], theme::INK);
    for (int i = 0; i < 2; ++i) ui::set_color(key_text_[i], theme::INK);
    ui::set_color(page_, theme::MUTED);
    ui::set_color(note_, theme::MUTED);
  }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (p.x < 0 || p.y < rows_top_) return;
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
    if (row < 0 || row >= count_) return;
    JsonDocument d;
    d["media_id"] = items_[row].uri;
    d["media_type"] = KINDS[request_.kind].type;
    if (tessera::action(PLAY, request_.player, d)) {
      plugin_->forget_popup();
      tessera::close_popup(plugin_);
    }
  }

  void on_dismiss() override { plugin_->forget_popup(); }

  // An answer of the app: {"re", "ok", "result": {"items": [{"name", "uri", ...}, ...]}}.
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

  // Ask for the page at offset_.
  void ask() {
    count_ = 0;
    fetched_ = 0;
    more_ = false;
    if (!request_.player[0] || !plugin_->config_entry()[0]) {
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
    request["data"]["config_entry_id"] = plugin_->config_entry();
    request["data"]["media_type"] = KINDS[request_.kind].type;
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
    // The note: why there is nothing to show, else nothing.
    const char *note = "";
    if (!request_.player[0]) note = "no_player";
    else if (!plugin_->config_entry()[0]) note = "no_entry";
    else if (state_ == State::FAILED) note = "failed";
    else if (count_ == 0) note = state_ == State::SHOWN ? "none" : "waiting";
    ui::set_text(note_, note[0] ? plugin_->text(note) : "");
    const bool rows = !note[0];
    for (int i = 0; i < rows_; ++i) ui::set_text(row_[i], rows && i < count_ ? items_[i].name : "");
    // The keys stand only where they do something; the page number only on a page.
    set_shown(0, rows && offset_ > 0);
    set_shown(1, rows && more_);
    ui::set_text(page_, rows ? tessera::format(plugin_->text("page"), offset_ / std::max(1, rows_) + 1) : "");
  }

  void set_shown(int key, bool shown) {
    if (shown) {
      lv_obj_clear_flag(key_[key], LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(key_text_[key], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(key_[key], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(key_text_[key], LV_OBJ_FLAG_HIDDEN);
    }
  }

  MaLibrary *plugin_;
  ListRequest request_;
  int offset_ = 0, count_ = 0, fetched_ = 0, rows_ = 1;
  bool more_ = false;
  State state_ = State::UNSENT;
  uint32_t asked_ = 0, asked_at_ = 0;
  int width_ = 0, gap_ = 0, bar_h_ = 0, row_h_ = 0, rows_top_ = 0, keys_y_ = 0, key_w_ = 0;
  Item items_[MAX_ROWS];
  lv_obj_t *title_{}, *row_[MAX_ROWS]{}, *key_[2]{}, *key_text_[2]{}, *page_{}, *note_{};
};

// The tile: a key with its title, the name it got in the editor or else the word of its list. A tap opens the list.
class LibraryTile : public tessera::Tile {
 public:
  explicit LibraryTile(MaLibrary *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    const char *list = c.options["list"] | "playlist";
    for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); ++i) {
      if (!strcmp(list, KINDS[i].type)) request_.kind = static_cast<int>(i);
    }
    const char *word = plugin_->text(KINDS[request_.kind].word);
    snprintf(request_.title, sizeof(request_.title), "%s", c.name && c.name[0] ? c.name : word);
    snprintf(request_.player, sizeof(request_.player), "%s", c.entity ? c.entity : "");

    // The largest font the title fits on a line in, or the body font over as many lines as it takes.
    const Font font = ui::text_width(request_.title, Font::TITLE) <= c.width ? Font::TITLE
                      : ui::text_width(request_.title, Font::BODY_LARGE) <= c.width ? Font::BODY_LARGE : Font::BODY;
    title_ = ui::text(c.parent, font, theme::INK);
    lv_obj_set_width(title_, c.width);
    lv_obj_set_height(title_, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    ui::set_text(title_, request_.title);
    lv_obj_center(title_);
    paint();
  }

  void on_state(JsonObjectConst data) override {
    // The player's own part: nothing of it is drawn but whether it can play.
    if (!data["state"].isNull()) unavailable_ = strcmp(data["state"] | "", "unavailable") == 0;
    paint();
  }

  void on_theme() override { paint(); }

  void on_tap() override { plugin_->open_list(request_); }

 private:
  void paint() { ui::set_color(title_, unavailable_ || !request_.player[0] ? theme::MUTED : theme::INK); }

  MaLibrary *plugin_;
  ListRequest request_;
  bool unavailable_ = false;
  lv_obj_t *title_{};
};

void MaLibrary::setup() {
  add_tile("library", [this]() { return new LibraryTile(this); });
}

void MaLibrary::open_list(const ListRequest &request) {
  tessera::show_popup(
      this,
      [this, request]() -> tessera::Popup * {
        popup_ = new ListPopup(this, request);
        return popup_;
      },
      tessera::PopupWidth::WIDE);
}

void MaLibrary::on_message(JsonObjectConst message) {
  if (popup_ != nullptr) popup_->answer(message);
}

}  // namespace esphome::ma_library
