#include "TimeSvc.h"

#include <time.h>

#include "../config.h"
#include "Logger.h"

void TimeSvc::begin(int32_t offsetSec) { setOffset(offsetSec); }

void TimeSvc::setOffset(int32_t offsetSec) {
  offset_ = offsetSec;
  configTime(offsetSec, 0, cfg::NTP_SERVER);
  LOGI("Timezone offset %ld s", (long)offsetSec);
}

bool TimeSvc::valid() const { return time(nullptr) > 1700000000; }

String TimeSvc::dateText() const {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char buf[40];
  snprintf(buf, sizeof(buf), "%d/%d/%d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  return buf;
}

String TimeSvc::timeText() const {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  return buf;
}

bool TimeSvc::parseOffset(const String& text, int32_t& outSec) {
  String s = text;
  s.trim();
  if (s.length() == 0) return false;

  int sign = 1;
  size_t i = 0;
  if (s[0] == '+' || s[0] == '-') {
    sign = s[0] == '-' ? -1 : 1;
    i = 1;
  }
  long hours = 0;
  size_t hDigits = 0;
  while (i < s.length() && isdigit((unsigned char)s[i]) && hDigits < 2) {
    hours = hours * 10 + (s[i] - '0');
    i++;
    hDigits++;
  }
  if (hDigits == 0) return false;

  long minutes = 0;
  if (i < s.length()) {
    if (s[i] != '.' && s[i] != ':') return false;
    i++;
    String frac = s.substring(i);
    if (frac.length() == 0 || frac.length() > 2) return false;
    for (size_t k = 0; k < frac.length(); k++)
      if (!isdigit((unsigned char)frac[k])) return false;
    long f = frac.toInt();
    if (frac.length() == 1)
      minutes = f * 6;  // tenths of an hour
    else if (f == 0 || f == 30 || f == 45)
      minutes = f;      // the only real-world minute offsets
    else
      minutes = f * 60 / 100;  // decimal hours, as v1 read them (".50" = 30 min)
  }
  long total = sign * (hours * 3600 + minutes * 60);
  if (total < -12 * 3600 || total > 14 * 3600) return false;
  outSec = total;
  return true;
}
