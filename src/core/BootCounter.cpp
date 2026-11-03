#include "BootCounter.h"

#include <LittleFS.h>
#include <user_interface.h>

#include "../config.h"
#include "Logger.h"

namespace {
const char kFile[] = "/boot.cnt";
}

uint8_t BootCounter::begin() {
  const rst_info* ri = ESP.getResetInfoPtr();
  bool userReset = ri->reason == REASON_DEFAULT_RST || ri->reason == REASON_EXT_SYS_RST;
  if (!userReset) {
    LOGI("Reset reason %s: not counted", ESP.getResetReason().c_str());
    LittleFS.remove(kFile);
    cleared_ = true;
    return 1;
  }

  uint8_t count = 0;
  File f = LittleFS.open(kFile, "r");
  if (f) {
    count = f.read();
    f.close();
    if (count == 0xFF) count = 0;
  }
  count++;
  f = LittleFS.open(kFile, "w");
  if (f) {
    f.write(count);
    f.close();
  }
  LOGI("Quick reset count: %u", count);
  return count;
}

void BootCounter::loop() {
  if (cleared_ || millis() < cfg::BOOT_COUNT_WINDOW_MS) return;
  LittleFS.remove(kFile);
  cleared_ = true;
}
