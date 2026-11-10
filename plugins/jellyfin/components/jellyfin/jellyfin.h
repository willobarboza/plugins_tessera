#pragma once
#include <cstdint>
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::jellyfin {

// What a tile hands its control card when it is tapped: a card gets the tile's data, not its options, so the tile says
// where in that data its player is (`slot`: "" for a now playing tile, whose data is the player itself; "session_3" for
// a row of the sessions tile) and how far the seek keys jump.
struct CardSetup {
  char slot[16] = "";
  int jump = 30;
};

// The card that waits for an answer of the app (the library card): answers to its questions go there.
class Listener {
 public:
  virtual ~Listener() = default;
  virtual void on_answer(JsonObjectConst message) = 0;
};

// The plugin: an ESPHome component and a tessera::Plugin. It registers its tiles and its cards in setup().
class Jellyfin : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override {
    if (listener_) listener_->on_answer(message);
  }

  CardSetup card_setup;
  void listen(Listener *l) { listener_ = l; }
  void forget(Listener *l) {
    if (listener_ == l) listener_ = nullptr;
  }

 private:
  Listener *listener_ = nullptr;
};

}  // namespace esphome::jellyfin
