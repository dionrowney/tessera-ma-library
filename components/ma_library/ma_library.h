#pragma once
#include <vector>
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::ma_library {

class LibraryTile;

// The plugin: the tile "library". The answers of tessera::send come to the plugin, so it keeps the tiles on the glass
// and hands each answer to all of them; a tile takes only its own (by "re").
class MaLibrary : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override;

  std::vector<LibraryTile *> tiles;
};

}  // namespace esphome::ma_library
