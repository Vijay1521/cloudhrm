// SSD1306 128x64 OLED screens. Screens and icons follow v1.
// A screen drawn with holdMs > 0 stays until it expires; while it is held,
// isHeld() is true and the app does not draw its idle screen over it.
#pragma once

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

class Ui {
 public:
  bool begin();
  bool isHeld() const;
  void release() { holdUntil_ = 0; }

  // Idle / status screens (redrawn by the app about once per second).
  void pleaseWait(const char* deviceId);
  void smartConfig(const char* deviceId, uint32_t secsLeft);
  void apMode(const String& ssid, const char* pass, uint32_t secsLeft);
  void connecting(const char* ssid);
  void networkProblem(const char* detail, uint32_t holdMs = 0);
  void needDeviceId(const String& ip);
  void clock(const String& date, const String& time, int bars, bool synced);

  // Event screens.
  void cardScanning(uint32_t holdMs = 0);
  void tapCard(uint32_t secsLeft);
  void fingerValid(uint32_t holdMs = 0);
  void fingerInvalid(uint32_t holdMs);
  void fingerFailed(uint32_t holdMs);
  void fingerScan(const char* caption);
  void removeFinger();
  void stored(uint32_t holdMs);
  void employeeName(const String& fullName, uint32_t holdMs);
  void message(const String& text, uint32_t holdMs);
  void idNotFound(uint32_t holdMs);
  void timezoneUpdated(uint32_t holdMs);
  void text2(const char* line1, const char* line2, uint32_t holdMs);
  void progress(const char* title, int percent);

  // 0..4 bars for an RSSI value in dBm (0 = not connected).
  static int barsForRssi(int32_t rssi);

 private:
  void start();
  void finish(uint32_t holdMs);
  void drawSignal(int x, int y, int bars);
  void countdown(uint32_t secs, uint16_t color);

  Adafruit_SSD1306 d_{128, 64, &Wire, -1};  // -1: no reset pin (v1 used GPIO0, the RC522 reset)
  bool ok_ = false;
  uint32_t holdUntil_ = 0;
};
