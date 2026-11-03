// ArduinoOTA on the office LAN (upload from Arduino IDE, VS Code or
// PlatformIO). Uses the portal admin password. Portal uploads are handled by
// Portal (/api/update).
#pragma once

#include <Arduino.h>

class Settings;
class Ui;

class Ota {
 public:
  void begin(Settings& settings, Ui& ui);
  // Call while online; starts the listener on first use.
  void loop();
  bool active() const { return started_; }

 private:
  Settings* s_ = nullptr;
  Ui* ui_ = nullptr;
  bool started_ = false;
};
