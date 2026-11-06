#pragma once
#include <cstdio>
#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::proxmox {

// What a node tile hands its card when it is tapped: a card gets the tile's data, not its options, so the tile passes
// the entity ids it needs (the three buttons, and the sensors whose history the card draws) just before open_card.
struct CardSetup {
  char keys[3][64] = {};   // start, shut down, restart: button entities
  char cpu[64] = "";       // the CPU sensor, for its hourly history
  char memory[64] = "";    // the memory sensor, the same
};

// The card, as the plugin sees it: answers to its questions go there.
class Listener {
 public:
  virtual ~Listener() = default;
  virtual void on_answer(JsonObjectConst message) = 0;
};

// The plugin: an ESPHome component and a tessera::Plugin. It registers its tiles, its card, its top bar item, its
// screensaver and its popup in setup(), and watches the problem sensor of plugin.yaml for the item and the popup.
class Proxmox : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override {
    if (listener_) listener_->on_answer(message);
  }
  void on_interval(uint32_t now_ms) override;
  bool settings(tessera::SettingsPage &page) override;

  void set_problem(binary_sensor::BinarySensor *s) { problem_ = s; }
  void set_problems(text_sensor::TextSensor *s) { problems_ = s; }
  void set_popup(switch_::Switch *s) { popup_ = s; }

  CardSetup card_setup;
  void listen(Listener *l) { listener_ = l; }
  void forget(Listener *l) {
    if (listener_ == l) listener_ = nullptr;
  }
  // Whether something is down now, and the names Home Assistant gave ("Docker, NAS").
  bool in_trouble() const { return problem_ && problem_->has_state() && problem_->state; }
  const char *problem_names() const {
    return problems_ && problems_->has_state() ? problems_->state.c_str() : "";
  }

 private:
  tessera::BarItem bar() const;
  Listener *listener_ = nullptr;
  binary_sensor::BinarySensor *problem_ = nullptr;
  text_sensor::TextSensor *problems_ = nullptr;
  switch_::Switch *popup_ = nullptr;
  bool was_in_trouble_ = false;
  char last_names_[160] = "";
};

}  // namespace esphome::proxmox
