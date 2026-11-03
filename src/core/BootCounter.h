// Counts quick resets (button presses or power cycles less than 3 s apart),
// used for the v1 reset gestures. Crash or watchdog resets are not counted.
#pragma once

#include <Arduino.h>

class BootCounter {
 public:
  // Call after LittleFS is mounted. Returns the number of quick resets,
  // including this boot (1 = normal boot).
  uint8_t begin();
  // Clears the counter once the device has run for the counting window.
  void loop();

 private:
  bool cleared_ = false;
};
