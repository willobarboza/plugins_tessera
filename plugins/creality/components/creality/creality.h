#pragma once
#include <cstdint>
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::creality {

// What a tile hands its control card when it is tapped: a card gets the tile's data, not its options, so the tile says
// which entities the keys act on.
struct CardSetup {
  char light[96] = "";
  char pause[96] = "";
  char resume[96] = "";
  char stop[96] = "";
};

// The plugin: an ESPHome component and a tessera::Plugin. It registers its tiles and its card in setup().
class Creality : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  CardSetup card_setup;
};

}  // namespace esphome::creality
