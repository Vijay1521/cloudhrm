// Door relay / buzzer on GPIO16, pulsed without blocking.
#pragma once

#include <Arduino.h>

class Relay {
 public:
  void begin(uint8_t pin);
  void pulse(uint16_t ms);
  void loop();

 private:
  uint8_t pin_ = 0;
  uint32_t offAt_ = 0;
  bool on_ = false;
};
