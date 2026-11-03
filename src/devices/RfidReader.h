// RC522 card reader. Card IDs use the v1 text format (each UID byte as
// lowercase hex without zero padding) so cards already registered on the
// server keep working.
#pragma once

#include <Arduino.h>
#include <MFRC522.h>

class RfidReader {
 public:
  void begin(uint8_t ssPin, uint8_t rstPin);
  // True when a new card was read; uid gets the v1-format text.
  bool poll(String& uid);
  uint8_t version() const { return version_; }

 private:
  void checkHealth();

  MFRC522* mfrc_ = nullptr;
  uint8_t version_ = 0;
  uint32_t lastPoll_ = 0;
  uint32_t lastHealth_ = 0;
};
