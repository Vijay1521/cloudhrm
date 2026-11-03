#include "NetManager.h"

#include <ESP8266WiFi.h>

#include "../config.h"
#include "../core/Logger.h"
#include "../core/Settings.h"

namespace {

const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApMask(255, 255, 255, 0);

WiFiPhyMode_t toSdkPhy(uint8_t m) {
  switch ((PhyMode)m) {
    case PhyMode::B: return WIFI_PHY_MODE_11B;
    case PhyMode::G: return WIFI_PHY_MODE_11G;
    default: return WIFI_PHY_MODE_11N;
  }
}

const char* phyLabel(uint8_t m) {
  switch ((PhyMode)m) {
    case PhyMode::B: return "802.11b";
    case PhyMode::G: return "802.11g";
    case PhyMode::N: return "802.11n";
    default: return "auto";
  }
}

bool due(uint32_t at) { return (int32_t)(millis() - at) >= 0; }

}  // namespace

void NetManager::begin(Settings& settings) {
  s_ = &settings;
  WiFi.persistent(false);  // credentials live in our settings, not SDK flash
  WiFi.setAutoConnect(false);
  WiFi.hostname(hostname());
}

String NetManager::apSsid() {
  char buf[20];
  snprintf(buf, sizeof(buf), "CloudHRM-%06X", ESP.getChipId());
  return buf;
}

String NetManager::hostname() {
  char buf[20];
  snprintf(buf, sizeof(buf), "cloudhrm-%06x", ESP.getChipId());
  return buf;
}

// ---- radio tuning for weak links ----------------------------------------

uint8_t NetManager::currentPhy() const {
  if (s_->data().phyMode != (uint8_t)PhyMode::Auto) return s_->data().phyMode;
  return tryPhy_;
}

void NetManager::applyRadioTuning() {
  WiFi.setSleepMode(WIFI_NONE_SLEEP);              // no missed beacons on marginal links
  WiFi.setPhyMode(toSdkPhy(currentPhy()));         // 11b: best sensitivity, +20 dBm
  WiFi.setOutputPower(s_->data().txPowerQdBm / 4.0f);  // default 20.5 dBm (maximum)
  WiFi.setAutoReconnect(true);
  const SettingsData& d = s_->data();
  if (d.useStaticIp)
    WiFi.config(IPAddress(d.staticIp), IPAddress(d.gateway), IPAddress(d.subnet), IPAddress(d.dns));
  else
    WiFi.config(IPAddress(), IPAddress(), IPAddress());  // all zero = DHCP
}

// ---- station ------------------------------------------------------------

void NetManager::connect() {
  if (!s_->hasWifi()) {
    startProvisioning(false);
    return;
  }
  uint8_t last = s_->data().lastGoodPhy;
  tryPhy_ = last != (uint8_t)PhyMode::Auto ? last : (uint8_t)PhyMode::B;
  failsOnPhy_ = 0;
  backoffStep_ = 0;
  if (offlineSince_ == 0) offlineSince_ = millis();
  startAttempt();
}

void NetManager::startAttempt() {
  state_ = State::Connecting;
  WiFi.mode(WIFI_STA);
  applyRadioTuning();

  const SettingsData& d = s_->data();
  // Join the strongest access point broadcasting our SSID (mesh / multi-AP).
  int best = -1;
  int n = WiFi.scanNetworks(false, true);
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == d.wifiSsid && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
  }
  if (best >= 0) {
    LOGI("Joining %s via %s ch%d %lddBm (%s)", d.wifiSsid, WiFi.BSSIDstr(best).c_str(), WiFi.channel(best),
         (long)WiFi.RSSI(best), phyLabel(currentPhy()));
    uint8_t bssid[6];
    memcpy(bssid, WiFi.BSSID(best), 6);
    int32_t ch = WiFi.channel(best);
    WiFi.scanDelete();
    WiFi.begin(d.wifiSsid, d.wifiPass, ch, bssid);
  } else {
    LOGI("Joining %s (not seen in scan, %s)", d.wifiSsid, phyLabel(currentPhy()));
    WiFi.scanDelete();
    WiFi.begin(d.wifiSsid, d.wifiPass);
  }
  attemptDeadline_ = millis() + cfg::WIFI_JOIN_TIMEOUT_MS;
}

void NetManager::loop() {
  uint32_t now = millis();
  switch (state_) {
    case State::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        state_ = State::Online;
        offlineSince_ = 0;
        backoffStep_ = 0;
        failsOnPhy_ = 0;
        lastError_ = "";
        LOGI("Online: IP %s, %lddBm, %s", WiFi.localIP().toString().c_str(), (long)WiFi.RSSI(),
             phyLabel(currentPhy()));
        if (s_->data().lastGoodPhy != currentPhy()) {
          s_->data().lastGoodPhy = currentPhy();
          s_->save();
        }
      } else if (due(attemptDeadline_)) {
        lastError_ = statusText(WiFi.status());
        LOGW("Join failed: %s", lastError_);
        WiFi.disconnect();
        // Some routers reject 11b; walk b -> g -> n -> b in auto mode.
        if (s_->data().phyMode == (uint8_t)PhyMode::Auto && ++failsOnPhy_ >= 3) {
          failsOnPhy_ = 0;
          tryPhy_ = tryPhy_ == (uint8_t)PhyMode::B ? (uint8_t)PhyMode::G
                    : tryPhy_ == (uint8_t)PhyMode::G ? (uint8_t)PhyMode::N
                                                    : (uint8_t)PhyMode::B;
          LOGI("Switching to %s", phyLabel(tryPhy_));
        }
        uint32_t wait = min<uint32_t>(2000UL << min<uint8_t>(backoffStep_, 5), 60000UL);
        backoffStep_++;
        retryAt_ = now + wait;
        state_ = State::Offline;
      }
      break;

    case State::Online:
      if (WiFi.status() != WL_CONNECTED) {
        if (lostAt_ == 0) {
          lostAt_ = now;
          offlineSince_ = now;
          LOGW("WiFi lost");
        } else if (now - lostAt_ > 15000) {  // let the SDK auto-reconnect first
          lostAt_ = 0;
          lastError_ = "Link lost";
          state_ = State::Offline;
          retryAt_ = now;
        }
      } else {
        if (lostAt_) {
          LOGI("WiFi back");
          offlineSince_ = 0;
        }
        lostAt_ = 0;
        pollRoaming();
      }
      break;

    case State::Offline:
      if (WiFi.status() == WL_CONNECTED) {
        state_ = State::Online;
        offlineSince_ = 0;
      } else if (due(retryAt_)) {
        startAttempt();
      }
      break;

    case State::Provisioning: {
      if (join_ == Join::Joining || join_ == Join::Ok) {
        pollJoin();
        break;
      }
      if (phase_ == Phase::SmartConfig && WiFi.smartConfigDone()) {
        // v1 payload: "<wifi password>$<deviceID>". Split on the last '$'
        // so passwords containing '$' still work.
        String raw = WiFi.psk();
        String ssid = WiFi.SSID();
        int cut = raw.lastIndexOf('$');
        String pass = cut >= 0 ? raw.substring(0, cut) : raw;
        joinDeviceId_ = cut >= 0 ? raw.substring(cut + 1) : "";
        joinDeviceId_.trim();
        LOGI("SmartConfig received for %s%s", ssid.c_str(), joinDeviceId_.length() ? " with device ID" : "");
        joinFromSc_ = true;
        joinSsid_ = ssid;
        joinPass_ = pass;
        join_ = Join::Joining;
        joinError_ = "";
        WiFi.begin(ssid, pass);  // SmartConfig stays running so the app receives its ack
        joinDeadline_ = now + cfg::WIFI_TEST_TIMEOUT_MS;
        break;
      }
      if (apOn_ && apClientConnected()) phaseEnd_ = max<uint32_t>(phaseEnd_, now + 30000);
      if (canGiveUp_ && now - provStart_ > cfg::PROVISIONING_GIVE_UP_MS && !apClientConnected()) {
        LOGI("Provisioning timed out, using saved WiFi");
        stopProvisioning();
        connect();
        break;
      }
      if (due(phaseEnd_)) enterPhase(phase_ == Phase::SmartConfig ? Phase::AccessPoint : Phase::SmartConfig);
      break;
    }

    case State::Idle:
      break;
  }

  if (now - lastSample_ >= 60000) {
    lastSample_ = now;
    sampleRssi();
  }
}

// ---- provisioning ---------------------------------------------------------

void NetManager::startProvisioning(bool canGiveUp) {
  LOGI("Provisioning mode");
  canGiveUp_ = canGiveUp && s_->hasWifi();
  provStart_ = millis();
  state_ = State::Provisioning;
  join_ = Join::None;
  WiFi.disconnect();
  enterPhase(Phase::SmartConfig);
}

void NetManager::enterPhase(Phase p) {
  phase_ = p;
  if (p == Phase::SmartConfig) {
    if (apOn_) {
      WiFi.softAPdisconnect(true);
      apOn_ = false;
    }
    WiFi.mode(WIFI_STA);
    WiFi.beginSmartConfig();
    phaseEnd_ = millis() + cfg::SMARTCONFIG_WINDOW_MS;
    LOGI("SmartConfig window open");
  } else {
    WiFi.stopSmartConfig();
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(kApIp, kApIp, kApMask);
    WiFi.softAP(apSsid(), s_->data().apPass);
    apOn_ = true;
    phaseEnd_ = millis() + cfg::AP_WINDOW_MS;
    LOGI("Setup hotspot %s up", apSsid().c_str());
  }
}

void NetManager::stopProvisioning() {
  WiFi.stopSmartConfig();
  if (apOn_) {
    WiFi.softAPdisconnect(true);
    apOn_ = false;
  }
  WiFi.mode(WIFI_STA);
}

uint32_t NetManager::phaseSecsLeft() const {
  int32_t left = (int32_t)(phaseEnd_ - millis());
  return left > 0 ? left / 1000 : 0;
}

bool NetManager::apClientConnected() const { return apOn_ && WiFi.softAPgetStationNum() > 0; }

void NetManager::requestJoin(const String& ssid, const String& pass) {
  joinFromSc_ = false;
  joinSsid_ = ssid;
  joinPass_ = pass;
  joinDeviceId_ = "";
  joinError_ = "";
  join_ = Join::Joining;
  if (state_ != State::Provisioning) {
    // Changing WiFi from the LAN portal: test without the hotspot.
    state_ = State::Provisioning;
    canGiveUp_ = false;
    provStart_ = millis();
  }
  LOGI("Testing WiFi %s", ssid.c_str());
  WiFi.disconnect();
  WiFi.begin(ssid, pass);
  joinDeadline_ = millis() + cfg::WIFI_TEST_TIMEOUT_MS;
}

void NetManager::pollJoin() {
  uint32_t now = millis();
  if (join_ == Join::Ok) {
    // Keep SmartConfig alive briefly for the app's ack, and the hotspot so the
    // portal can show the result, then switch to normal station mode.
    if (now - joinOkAt_ > (joinFromSc_ ? 5000UL : 15000UL)) {
      stopProvisioning();
      connect();
    }
    return;
  }
  int st = WiFi.status();
  if (st == WL_CONNECTED) {
    SettingsData& d = s_->data();
    copyStr(d.wifiSsid, joinSsid_);
    copyStr(d.wifiPass, joinPass_);
    if (joinDeviceId_.length() && Settings::validDeviceId(joinDeviceId_)) copyStr(d.deviceId, joinDeviceId_);
    d.lastGoodPhy = (uint8_t)PhyMode::Auto;
    s_->save();
    join_ = Join::Ok;
    joinOkAt_ = now;
    LOGI("WiFi credentials verified and saved");
    return;
  }
  if (st == WL_WRONG_PASSWORD || due(joinDeadline_)) {
    joinError_ = st == WL_WRONG_PASSWORD ? "Wrong password" : statusText(st);
    join_ = Join::Failed;
    LOGW("WiFi test failed: %s", joinError_);
    WiFi.disconnect();
    if (joinFromSc_) {
      WiFi.stopSmartConfig();
      enterPhase(Phase::SmartConfig);
    } else if (!apOn_ && s_->hasWifi()) {
      connect();  // LAN change failed: go back to the saved network
    }
  }
}

// ---- scanning, roaming, diagnostics --------------------------------------

void NetManager::startScan() {
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  roamScan_ = false;
  scanForPortal_ = true;
  WiFi.scanNetworks(true, true);
}

int NetManager::scanResult() const {
  if (!scanForPortal_) return -2;
  return WiFi.scanComplete();
}

void NetManager::pollRoaming() {
  if (roamScan_) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    roamScan_ = false;
    int best = -1;
    for (int i = 0; i < n; i++)
      if (WiFi.SSID(i) == s_->data().wifiSsid && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
    if (best >= 0 && WiFi.BSSIDstr(best) != WiFi.BSSIDstr() && WiFi.RSSI(best) >= WiFi.RSSI() + 8) {
      LOGI("Roaming to %s (%lddBm)", WiFi.BSSIDstr(best).c_str(), (long)WiFi.RSSI(best));
      WiFi.scanDelete();
      startAttempt();
      return;
    }
    WiFi.scanDelete();
    return;
  }
  if (weakSamples_ >= 3 && !scanForPortal_ && millis() - lastRoamScan_ > 300000) {
    lastRoamScan_ = millis();
    weakSamples_ = 0;
    roamScan_ = true;
    WiFi.scanNetworks(true, false);
  }
}

void NetManager::sampleRssi() {
  int8_t r = online() ? (int8_t)WiFi.RSSI() : 0;
  if (histCount_ < sizeof(hist_)) {
    hist_[histCount_++] = r;
  } else {
    memmove(hist_, hist_ + 1, sizeof(hist_) - 1);
    hist_[sizeof(hist_) - 1] = r;
  }
  weakSamples_ = (r != 0 && r < cfg::ROAM_RSSI_THRESHOLD) ? weakSamples_ + 1 : 0;
}

uint8_t NetManager::rssiHistory(int8_t* out, uint8_t max) const {
  uint8_t n = min<uint8_t>(histCount_, max);
  memcpy(out, hist_ + (histCount_ - n), n);
  return n;
}

int32_t NetManager::rssi() const { return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0; }

const char* NetManager::phyName() const { return phyLabel(currentPhy()); }

const char* NetManager::statusText(int status) {
  switch (status) {
    case WL_NO_SSID_AVAIL: return "Network not found";
    case WL_WRONG_PASSWORD: return "Wrong password";
    case WL_CONNECT_FAILED: return "Connect failed";
    case WL_DISCONNECTED: return "No answer from router";
    case WL_IDLE_STATUS: return "No IP address (DHCP)";
    default: return "Timeout";
  }
}
