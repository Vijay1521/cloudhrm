#include "Ota.h"

#include <ArduinoOTA.h>
#include <MD5Builder.h>

#include "../config.h"
#include "../net/NetManager.h"
#include "../ui/Ui.h"
#include "Logger.h"
#include "Settings.h"

void Ota::begin(Settings& settings, Ui& ui) {
  s_ = &settings;
  ui_ = &ui;
}

void Ota::loop() {
  if (!started_) {
    if (!s_->flag(FLAG_ARDUINO_OTA)) return;
    ArduinoOTA.setHostname(NetManager::hostname().c_str());
    // Admin password once one is set, otherwise the master password.
    if (s_->data().adminSet) {
      ArduinoOTA.setPasswordHash(s_->data().otaMd5);
    } else {
      MD5Builder md5;
      md5.begin();
      md5.add(cfg::MASTER_PASSWORD);
      md5.calculate();
      ArduinoOTA.setPasswordHash(md5.toString().c_str());
    }
    ArduinoOTA.onStart([this]() {
      LOGI("ArduinoOTA start");
      ui_->progress("Updating", 0);
    });
    ArduinoOTA.onProgress([this](unsigned int done, unsigned int total) {
      static int lastPct = -1;
      int pct = total ? (int)(done * 100ULL / total) : 0;
      if (pct != lastPct) {
        lastPct = pct;
        ui_->progress("Updating", pct);
      }
    });
    ArduinoOTA.onEnd([this]() {
      LOGI("ArduinoOTA done, rebooting");
      ui_->text2("Update", "done", 0);
    });
    ArduinoOTA.onError([](ota_error_t e) { LOGE("ArduinoOTA error %u", (unsigned)e); });
    ArduinoOTA.begin();
    started_ = true;
    LOGI("ArduinoOTA listening as %s", NetManager::hostname().c_str());
  }
  ArduinoOTA.handle();
}
