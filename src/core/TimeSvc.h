// Wall-clock time from SNTP (asia.pool.ntp.org, as in v1) plus the
// timezone offset the server sends over MQTT (RTC_update).
#pragma once

#include <Arduino.h>

class TimeSvc {
 public:
  void begin(int32_t offsetSec);
  void setOffset(int32_t offsetSec);
  int32_t offset() const { return offset_; }
  bool valid() const;

  // v1 screen formats: "d/m/yyyy" and "HH:MM:SS".
  String dateText() const;
  String timeText() const;

  // Parses RTC_update values such as "+05.30", "+05:30", "-04.00", "+05.5".
  // ".00", ".30", ".45" are minutes (the only real-world offsets); any other
  // fraction is decimal hours as in v1 (".5" or ".50" = 30 min, ".75" = 45 min).
  // Accepts -12:00 .. +14:00.
  static bool parseOffset(const String& text, int32_t& outSec);

 private:
  int32_t offset_ = 0;
};
