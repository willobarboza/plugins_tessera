#pragma once
#include <cstdint>
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::creality {

// What a tile hands its control card when it is tapped: a card gets the tile's data, not its options, so the tile says
// where in that data the printer's status is (`status`: "" for a printer tile, whose own entity is the status sensor;
// "status" for a camera tile, whose status is a chosen entity) and which entities the keys act on.
struct CardSetup {
  char status[12] = "";
  char light[96] = "";
  char pause[96] = "";
  char resume[96] = "";
  char stop[96] = "";
  bool camera = false;  // opened from a camera tile: the card's entity is the camera, which it shows
};

// The plugin: an ESPHome component and a tessera::Plugin. It registers its tiles and its card in setup().
class Creality : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  CardSetup card_setup;
};

}  // namespace esphome::creality
