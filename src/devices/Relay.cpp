#include "Relay.h"

void Relay::begin(uint8_t pin) {
  pin_ = pin;
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}

void Relay::pulse(uint16_t ms) {
  digitalWrite(pin_, HIGH);
  on_ = true;
  offAt_ = millis() + ms;
}

void Relay::loop() {
  if (on_ && (int32_t)(millis() - offAt_) >= 0) {
    digitalWrite(pin_, LOW);
    on_ = false;
  }
}
