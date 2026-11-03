#include "App.h"

#include <ESP8266WiFi.h>

#include "../config.h"
#include "../core/Logger.h"

namespace {
bool due(uint32_t at) { return (int32_t)(millis() - at) >= 0; }
}  // namespace

// ---- startup ------------------------------------------------------------

void App::begin() {
  Log::begin();
  LOGI("CloudHRM v%s, chip %06X, reset: %s", CHRM_FW_VERSION, ESP.getChipId(), ESP.getResetReason().c_str());

  ui_.begin();
  settings_.begin();
  ui_.pleaseWait(settings_.data().deviceId);
  uint8_t resets = boot_.begin();

  relay_.begin(cfg::PIN_RELAY);
  rfid_.begin(cfg::PIN_RFID_SS, cfg::PIN_RFID_RST);
  bio_.begin(ui_);

  time_.begin(settings_.data().tzOffsetSec);
  api_.begin(settings_);
  net_.begin(settings_);
  mqtt_.begin(settings_, jobs_);
  ota_.begin(settings_, ui_);
  githubOta_.begin(settings_, ui_);
  githubOta_.onBeforeInstall([this]() { mqtt_.disconnect(); });

  PortalHooks hooks;
  hooks.fillStatus = [this](JsonObject o) { fillStatus(o); };
  hooks.busy = [this]() { return phase_ != JobPhase::None; };
  hooks.settingsChanged = [this]() { onSettingsChanged(); };
  portal_.begin(settings_, net_, ui_, githubOta_, hooks);

  handleResetGesture(resets);
}

void App::handleResetGesture(uint8_t resets) {
  if (resets >= cfg::RESETS_FACTORY) {
    LOGW("%u quick resets: factory reset", resets);
    settings_.factoryReset();
    ui_.text2("Factory", "reset", 2000);
    net_.startProvisioning(false);
  } else if (resets == cfg::RESETS_WIFI_WIPE) {
    LOGW("%u quick resets: WiFi credentials cleared", resets);
    settings_.clearWifi();
    ui_.text2("WiFi", "cleared", 2000);
    net_.startProvisioning(false);
  } else if (resets == cfg::RESETS_PROVISION || !settings_.hasWifi()) {
    net_.startProvisioning(true);
  } else {
    net_.connect();
  }
}

// ---- main loop ----------------------------------------------------------

void App::loop() {
  boot_.loop();
  net_.loop();
  if (net_.online()) {
    mqtt_.loop();
    ota_.loop();
    registerDevice();
  }
  portal_.loop();
  githubOta_.loop(net_.online(), phase_ == JobPhase::None, millis() - lastPunchAt_ > cfg::AUTO_UPDATE_IDLE_MS);
  relay_.loop();
  watchJoinResult();
  runJobs();
  if (phase_ == JobPhase::None) attendance();
  drawIdle();
}

void App::registerDevice() {
  if (registered_ || !settings_.hasDeviceId() || !due(nextRegTry_)) return;
  ApiResult r = api_.updateDeviceConfig(WiFi.macAddress());
  // Any server reply ends it (v1 ignored the reply); only retry when the
  // request never got an answer.
  if (r.http > 0) {
    registered_ = true;
  } else {
    nextRegTry_ = millis() + regBackoff_;
    regBackoff_ = min<uint32_t>(regBackoff_ * 2, 300000UL);
  }
}

void App::onSettingsChanged() {
  if (settings_.data().tzOffsetSec != time_.offset()) time_.setOffset(settings_.data().tzOffsetSec);
  mqtt_.disconnect();  // resubscribe with the new device ID
  registered_ = false;
  nextRegTry_ = 0;
}

// ---- attendance -----------------------------------------------------------

void App::attendance() {
  String uid;
  if (rfid_.poll(uid)) {
    if (uid == lastCard_ && millis() - lastCardAt_ < cfg::PUNCH_DEBOUNCE_MS) return;
    lastCard_ = uid;
    lastCardAt_ = millis();
    LOGI("Card %s", uid.c_str());
    ui_.cardScanning();
    punch(uid, 'R');
    return;
  }

  uint16_t id = 0;
  switch (bio_.identify(id)) {
    case Biometric::Scan::Match:
      if (id == lastFinger_ && millis() - lastFingerAt_ < cfg::PUNCH_DEBOUNCE_MS) return;
      lastFinger_ = id;
      lastFingerAt_ = millis();
      LOGI("Fingerprint slot %u", id);
      ui_.fingerValid();
      punch(String(id), 'B');
      break;
    case Biometric::Scan::NoMatch:
      ui_.fingerInvalid(1000);
      break;
    case Biometric::Scan::Error:
      ui_.fingerFailed(1000);
      break;
    case Biometric::Scan::None:
      break;
  }
}

void App::punch(const String& accessId, char type) {
  uint16_t relayMs = type == 'R' ? settings_.data().relayRfidMs : settings_.data().relayFpMs;
  lastPunchAt_ = millis();
  ApiResult r;
  if (!settings_.hasDeviceId()) {
    ui_.text2("No device", "ID set", cfg::RESULT_SCREEN_MS);
    return;
  }
  if (!net_.online()) {
    r.http = -1;
  } else {
    r = api_.markAttendance(accessId, type);
  }

  if (historyCount_ == 5) {
    for (int i = 0; i < 4; i++) history_[i] = history_[i + 1];
    historyCount_ = 4;
  }
  history_[historyCount_++] = Punch{millis(), type, accessId, r.http};

  showResult(r, relayMs);
}

void App::showResult(const ApiResult& r, uint16_t relayMs) {
  if (r.ok()) {
    if (r.fullName.length())
      ui_.employeeName(r.fullName, cfg::RESULT_SCREEN_MS);
    else
      ui_.text2("Thank", "you", cfg::RESULT_SCREEN_MS);
    relay_.pulse(relayMs);
  } else if (r.http == 400) {
    ui_.message(r.message.length() ? r.message : String("Not allowed"), cfg::RESULT_SCREEN_MS);
  } else if (r.networkError()) {
    ui_.networkProblem("Try again", cfg::RESULT_SCREEN_MS);
  } else {
    ui_.idNotFound(cfg::RESULT_SCREEN_MS);
  }
}

// ---- remote jobs (from MQTT) ----------------------------------------------

void App::runJobs() {
  if (phase_ == JobPhase::None) {
    Job j;
    if (jobs_.pop(j)) startJob(j);
    return;
  }

  if (phase_ == JobPhase::FingerEnroll) {
    Biometric::EnrollStatus st = bio_.pollEnroll();
    if (st != Biometric::EnrollStatus::Running) finishFingerEnroll(st == Biometric::EnrollStatus::Success);
    return;
  }

  if (phase_ == JobPhase::CardWait) {
    String uid;
    if (rfid_.poll(uid)) {
      LOGI("Enroll card %s for employee %s", uid.c_str(), job_.employeeId.c_str());
      ui_.cardScanning();
      ApiResult r;
      r.http = -1;  // shown as "Network problem" when offline
      if (net_.online()) r = api_.registerRfid(job_.employeeId, job_.deviceType, uid);
      showResult(r, settings_.data().relayRfidMs);
      phase_ = JobPhase::None;
    } else if (due(jobDeadline_)) {
      LOGW("Card enroll timed out");
      ui_.text2("Card", "timeout", cfg::RESULT_SCREEN_MS);
      phase_ = JobPhase::None;
    }
  }
}

void App::startJob(const Job& job) {
  job_ = job;
  switch (job.type) {
    case JobType::SetTimezone: {
      int32_t sec;
      if (TimeSvc::parseOffset(job.value, sec)) {
        settings_.data().tzOffsetSec = sec;
        settings_.save();
        time_.setOffset(sec);
        ui_.timezoneUpdated(cfg::RESULT_SCREEN_MS);
      } else {
        LOGW("Bad RTC_update '%s'", job.value.c_str());
      }
      break;
    }

    case JobType::EnrollCard:
      LOGI("Card enroll for employee %s", job.employeeId.c_str());
      jobDeadline_ = millis() + cfg::RFID_ENROLL_WINDOW_MS;
      phase_ = JobPhase::CardWait;
      lastIdleDraw_ = 0;
      break;

    case JobType::EnrollFinger: {
      LOGI("Fingerprint enroll for employee %s", job.employeeId.c_str());
      ui_.text2("Enroll", "wait...", 0);
      ApiResult r = api_.getBiometricId(job.employeeId, job.deviceType);
      if (r.biometricId > 0 && r.biometricId <= 0xFFFF && (bio_.capacity() == 0 || r.biometricId < bio_.capacity())) {
        enrollSlot_ = (uint16_t)r.biometricId;
        bio_.startEnroll(enrollSlot_);
        phase_ = JobPhase::FingerEnroll;
      } else if (r.networkError()) {
        ui_.networkProblem("Try again", cfg::RESULT_SCREEN_MS);
      } else {
        ui_.message(r.message.length() ? r.message : String("No biometric ID for this employee"), cfg::RESULT_SCREEN_MS);
      }
      break;
    }
  }
}

void App::finishFingerEnroll(bool ok) {
  phase_ = JobPhase::None;
  if (!ok) {
    ui_.message(String("Enroll failed: ") + bio_.enrollError(), cfg::RESULT_SCREEN_MS);
    return;
  }
  ApiResult r = api_.registerBiometric(job_.employeeId, enrollSlot_, job_.deviceType);
  showResult(r, settings_.data().relayRfidMs);
  fpTemplatesAt_ = 0;
}

// ---- screens --------------------------------------------------------------

void App::watchJoinResult() {
  NetManager::Join j = net_.joinState();
  if (j == lastJoin_) return;
  lastJoin_ = j;
  if (j == NetManager::Join::Failed) ui_.message(String("WiFi failed: ") + net_.joinError(), 3000);
  if (j == NetManager::Join::Ok) ui_.text2("WiFi", "connected", 3000);
}

void App::drawIdle() {
  if (ui_.isHeld() || phase_ == JobPhase::FingerEnroll) return;
  uint32_t now = millis();
  if (lastIdleDraw_ && now - lastIdleDraw_ < 1000) return;
  lastIdleDraw_ = now;
  const SettingsData& d = settings_.data();

  switch (net_.state()) {
    case NetManager::State::Provisioning:
      if (net_.joinState() == NetManager::Join::Joining)
        ui_.connecting(net_.joinSsid().c_str());
      else if (net_.phase() == NetManager::Phase::SmartConfig)
        ui_.smartConfig(d.deviceId, net_.phaseSecsLeft());
      else
        ui_.apMode(NetManager::apSsid(), d.apPass, net_.phaseSecsLeft());
      break;

    case NetManager::State::Online:
      if (!settings_.hasDeviceId())
        ui_.needDeviceId(WiFi.localIP().toString());
      else if (phase_ == JobPhase::CardWait)
        ui_.tapCard((int32_t)(jobDeadline_ - now) > 0 ? (jobDeadline_ - now) / 1000 : 0);
      else
        ui_.clock(time_.dateText(), time_.timeText(), Ui::barsForRssi(net_.rssi()), time_.valid());
      break;

    default:
      if (net_.offlineSinceMs() && now - net_.offlineSinceMs() > 30000)
        ui_.networkProblem(net_.lastError());
      else
        ui_.pleaseWait(d.deviceId);
      break;
  }
}

// ---- portal status ----------------------------------------------------------

void App::fillStatus(JsonObject o) {
  const SettingsData& d = settings_.data();
  char chip[8];
  snprintf(chip, sizeof(chip), "%06X", ESP.getChipId());
  bool connected = WiFi.status() == WL_CONNECTED;

  o["fw"] = CHRM_FW_VERSION;
  o["deviceId"] = d.deviceId;
  o["chipId"] = chip;
  o["timeSynced"] = time_.valid();
  o["time"] = time_.dateText() + " " + time_.timeText();
  o["tzOffsetSec"] = d.tzOffsetSec;
  o["uptime"] = millis() / 1000;

  o["ssid"] = connected ? WiFi.SSID() : String(d.wifiSsid);
  o["rssi"] = net_.rssi();
  static const char* const states[] = {"Idle", "Connecting", "Online", "Offline", "Setup mode"};
  o["netState"] = states[(int)net_.state()];
  o["netError"] = net_.online() ? "" : net_.lastError();
  o["ip"] = connected ? WiFi.localIP().toString() : String();
  o["mac"] = WiFi.macAddress();
  o["phy"] = net_.phyName();
  o["channel"] = connected ? WiFi.channel() : 0;
  o["mqtt"] = mqtt_.connected();
  JsonObject a = o["api"].to<JsonObject>();
  a["lastHttp"] = api_.lastHttp();
  a["lastMs"] = api_.lastMs();
  int8_t hist[10];
  uint8_t n = net_.rssiHistory(hist, sizeof(hist));
  JsonArray h = o["rssiHistory"].to<JsonArray>();
  for (uint8_t i = 0; i < n; i++) h.add(hist[i]);

  JsonArray p = o["punches"].to<JsonArray>();
  for (int i = historyCount_ - 1; i >= 0; i--) {
    JsonObject e = p.add<JsonObject>();
    e["ago"] = (millis() - history_[i].at) / 1000;
    e["type"] = String(history_[i].type);
    e["id"] = history_[i].id;
    e["http"] = history_[i].http;
  }

  o["heap"] = ESP.getFreeHeap();
  o["heapFrag"] = ESP.getHeapFragmentation();
  o["resetReason"] = ESP.getResetReason();
  o["flashSize"] = ESP.getFlashChipRealSize();
  o["sketchSize"] = ESP.getSketchSize();
  o["freeSketch"] = ESP.getFreeSketchSpace();
  o["storageOk"] = settings_.storageOk();

  if (phase_ == JobPhase::None && (fpTemplatesAt_ == 0 || millis() - fpTemplatesAt_ > 30000)) {
    fpTemplates_ = bio_.templateCount();
    fpTemplatesAt_ = millis();
  }
  JsonObject fp = o["fp"].to<JsonObject>();
  fp["present"] = bio_.present();
  fp["templates"] = fpTemplates_;
  fp["capacity"] = bio_.capacity();
  o["rfidVersion"] = (rfid_.version() == 0 || rfid_.version() == 0xFF) ? 0 : rfid_.version();
  o["jobs"] = jobs_.size();
  o["busy"] = phase_ != JobPhase::None;
}
