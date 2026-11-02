#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::ma_library {

class ListPopup;
struct ListRequest;

// The plugin: the tile "library", a key that opens a list of Music Assistant's library in a popup. One popup shows at a
// time, so the plugin keeps it and hands it the answers of tessera::send; it takes only its own (by "re").
class MaLibrary : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override;

  // The Music Assistant integration's config entry id, the plugin's input: a text in the firmware, never freed.
  void set_config_entry(const char *entry) { config_entry_ = entry; }
  const char *config_entry() const { return config_entry_; }

  void open_list(const ListRequest &request);
  // The popup is going: the core deletes it.
  void forget_popup() { popup_ = nullptr; }

 private:
  const char *config_entry_{""};
  ListPopup *popup_{};
};

}  // namespace esphome::ma_library
