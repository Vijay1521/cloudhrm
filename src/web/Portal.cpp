#include "Portal.h"

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiUdp.h>

#include "../cloud/CloudApi.h"
#include "../config.h"
#include "../core/GithubOta.h"
#include "../core/Logger.h"
#include "../core/Settings.h"
#include "../net/NetManager.h"
#include "../ui/Ui.h"
#include "portal_html.h"

namespace {

ESP8266WebServer server(80);
DNSServer dns;

bool due(uint32_t at) { return at && (int32_t)(millis() - at) >= 0; }

}  // namespace

void Portal::begin(Settings& settings, NetManager& net, Ui& ui, GithubOta& githubOta, PortalHooks hooks) {
  s_ = &settings;
  net_ = &net;
  ui_ = &ui;
  gh_ = &githubOta;
  hooks_ = hooks;
  server.collectHeaders("Authorization");
  routes();
  server.begin();
}

void Portal::loop() {
  bool ap = net_->apActive();
  if (ap && !dnsOn_) {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());  // every name -> the portal (captive portal)
    dnsOn_ = true;
  } else if (!ap && dnsOn_) {
    dns.stop();
    dnsOn_ = false;
  }
  if (dnsOn_) dns.processNextRequest();
  server.handleClient();
  if (due(rebootAt_)) {
    LOGI("Rebooting (portal request)");
    delay(100);
    ESP.restart();
  }
}

// ---- access control -------------------------------------------------------

bool Portal::viaHotspot() { return net_->apActive() && server.client().localIP() == WiFi.softAPIP(); }

bool Portal::allowed() { return viaHotspot() || s_->flag(FLAG_LAN_PORTAL); }

bool Portal::authorized() {
  const String& h = server.header("Authorization");
  if (!h.startsWith("Bearer ") || h.length() != 7 + 32) return false;
  const char* tok = h.c_str() + 7;
  uint32_t now = millis();
  for (Session& s : sessions_) {
    if (s.token[0] && strcmp(s.token, tok) == 0) {
      if (now - s.lastUsed > cfg::SESSION_IDLE_MS) {
        s.token[0] = 0;
        return false;
      }
      s.lastUsed = now;
      return true;
    }
  }
  return false;
}

bool Portal::requireAuth() {
  if (!allowed()) {
    sendError(403, "Portal disabled on LAN");
    return false;
  }
  if (!authorized()) {
    sendError(401, "Login required");
    return false;
  }
  return true;
}

String Portal::newSession() {
  uint8_t raw[16];
  ESP.random(raw, sizeof(raw));
  char tok[33];
  for (int i = 0; i < 16; i++) sprintf(tok + i * 2, "%02x", raw[i]);
  // Replace the least recently used slot.
  Session* slot = &sessions_[0];
  for (Session& s : sessions_)
    if (!s.token[0] || s.lastUsed < slot->lastUsed) slot = &s;
  strcpy(slot->token, tok);
  slot->lastUsed = millis();
  return tok;
}

// ---- helpers ----------------------------------------------------------------

bool Portal::parseBody(JsonDocument& doc) {
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok || !doc.is<JsonObject>()) {
    sendError(400, "Bad JSON");
    return false;
  }
  return true;
}

void Portal::sendJson(int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

void Portal::sendError(int code, const char* msg) {
  JsonDocument doc;
  doc["error"] = msg;
  sendJson(code, doc);
}

// ---- routes -------------------------------------------------------------

void Portal::routes() {
  server.on("/", HTTP_GET, [this] { handleIndex(); });
  server.on("/api/info", HTTP_GET, [this] { handleInfo(); });
  server.on("/api/setup", HTTP_POST, [this] { handleSetup(); });
  server.on("/api/login", HTTP_POST, [this] { handleLogin(); });
  server.on("/api/logout", HTTP_POST, [this] { handleLogout(); });
  server.on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server.on("/api/scan", HTTP_POST, [this] { handleScanStart(); });
  server.on("/api/scan", HTTP_GET, [this] { handleScanResult(); });
  server.on("/api/wifi", HTTP_POST, [this] { handleWifi(); });
  server.on("/api/wifi/result", HTTP_GET, [this] { handleWifiResult(); });
  server.on("/api/device", HTTP_GET, [this] { handleDeviceGet(); });
  server.on("/api/device", HTTP_POST, [this] { handleDevicePost(); });
  server.on("/api/logs", HTTP_GET, [this] { handleLogs(); });
  server.on("/api/password", HTTP_POST, [this] { handlePassword(); });
  server.on("/api/ping", HTTP_POST, [this] { handlePing(); });
  server.on("/api/reboot", HTTP_POST, [this] { handleReboot(); });
  server.on("/api/reset-wifi", HTTP_POST, [this] { handleResetWifi(); });
  server.on("/api/factory-reset", HTTP_POST, [this] { handleFactoryReset(); });
  server.on("/api/update", HTTP_POST, [this] { handleUpdateDone(); }, [this] { handleUpdateUpload(); });
  server.on("/api/github-ota", HTTP_GET, [this] { handleGithubStatus(); });
  server.on("/api/github-ota/check", HTTP_POST, [this] { handleGithubCheck(); });
  server.on("/api/github-ota/install", HTTP_POST, [this] { handleGithubInstall(); });
  server.onNotFound([this] { handleNotFound(); });
}

void Portal::handleIndex() {
  if (!allowed()) {
    server.send(403, "text/plain", "Portal disabled on LAN");
    return;
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("X-Frame-Options", "DENY");
  server.send_P(200, "text/html", reinterpret_cast<const char*>(PORTAL_HTML_GZ), PORTAL_HTML_GZ_LEN);
}

void Portal::handleNotFound() {
  if (viaHotspot()) {
    // Phones probe /generate_204, /hotspot-detect.html, ... -> open the portal.
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Not found");
}

void Portal::handleInfo() {
  JsonDocument doc;
  doc["fw"] = CHRM_FW_VERSION;
  doc["claimed"] = s_->data().adminSet != 0;
  doc["viaHotspot"] = viaHotspot();
  doc["lanPortal"] = s_->flag(FLAG_LAN_PORTAL);
  doc["name"] = NetManager::apSsid();
  sendJson(200, doc);
}

void Portal::handleSetup() {
  // The first admin password can only be set from the hotspot, whose WPA2
  // key is shown on the OLED (physical access to the device).
  if (s_->data().adminSet) return sendError(409, "Already set up");
  if (!viaHotspot()) return sendError(403, "Connect to the device hotspot to set it up");
  JsonDocument in;
  if (!parseBody(in)) return;
  String pw = in["password"] | "";
  if (!s_->setAdminPassword(pw)) return sendError(400, "Password must be 8-64 characters");
  LOGI("Admin password set");
  JsonDocument out;
  out["token"] = newSession();
  sendJson(200, out);
}

void Portal::handleLogin() {
  if (!allowed()) return sendError(403, "Portal disabled on LAN");
  if (lockedUntil_ && !due(lockedUntil_)) return sendError(429, "Too many attempts, wait 5 minutes");
  JsonDocument in;
  if (!parseBody(in)) return;
  String pw = in["password"] | "";
  bool master = Settings::checkMasterPassword(pw);
  if (!master && !s_->checkAdminPassword(pw)) {
    if (++loginFails_ >= cfg::LOGIN_MAX_FAILS) {
      loginFails_ = 0;
      lockedUntil_ = millis() + cfg::LOGIN_LOCKOUT_MS;
      LOGW("Portal login locked for 5 minutes");
    }
    delay(400);  // slow down guessing
    return sendError(401, "Wrong password");
  }
  loginFails_ = 0;
  lockedUntil_ = 0;
  if (master) LOGI("Portal login with master password");
  JsonDocument out;
  out["token"] = newSession();
  sendJson(200, out);
}

void Portal::handleLogout() {
  const String& h = server.header("Authorization");
  for (Session& s : sessions_)
    if (s.token[0] && h.endsWith(s.token)) s.token[0] = 0;
  server.send(204, "text/plain", "");
}

void Portal::handleStatus() {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (hooks_.fillStatus) hooks_.fillStatus(doc.to<JsonObject>());
  sendJson(200, doc);
}

// ---- WiFi -----------------------------------------------------------------

void Portal::handleScanStart() {
  if (!requireAuth()) return;
  net_->startScan();
  server.send(202, "application/json", "{}");
}

void Portal::handleScanResult() {
  if (!requireAuth()) return;
  int n = net_->scanResult();
  JsonDocument doc;
  doc["scanning"] = n == WIFI_SCAN_RUNNING;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;  // hidden
    // Keep only the strongest entry per SSID.
    bool dup = false;
    for (JsonObject o : list) {
      if (ssid == o["ssid"].as<const char*>()) {
        dup = true;
        if (WiFi.RSSI(i) > o["rssi"].as<int>()) {
          o["rssi"] = WiFi.RSSI(i);
          o["ch"] = WiFi.channel(i);
        }
        break;
      }
    }
    if (dup) continue;
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["ch"] = WiFi.channel(i);
    o["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  sendJson(200, doc);
}

void Portal::handleWifi() {
  if (!requireAuth()) return;
  JsonDocument in;
  if (!parseBody(in)) return;
  String ssid = in["ssid"] | "";
  String pass = in["password"] | "";
  String deviceId = in["deviceId"] | "";
  deviceId.trim();
  if (!Settings::validSsid(ssid)) return sendError(400, "SSID must be 1-32 characters");
  if (!Settings::validWifiPass(pass)) return sendError(400, "Password must be 8-63 characters (or empty for open WiFi)");
  if (deviceId.length() && !Settings::validDeviceId(deviceId)) return sendError(400, "Device ID: 1-64 printable characters");
  if (!deviceId.length() && !s_->hasDeviceId()) return sendError(400, "Device ID is required");

  SettingsData& d = s_->data();
  if (deviceId.length() && deviceId != d.deviceId) {
    copyStr(d.deviceId, deviceId);
    if (hooks_.settingsChanged) hooks_.settingsChanged();
  }
  JsonVariant st = in["staticIp"];
  if (st.is<JsonObject>() && st["enabled"].as<bool>()) {
    IPAddress ip, gw, mask, dnsIp;
    if (!ip.fromString(st["ip"] | "") || !gw.fromString(st["gateway"] | "") || !mask.fromString(st["subnet"] | ""))
      return sendError(400, "Static IP: invalid address");
    if (!dnsIp.fromString(st["dns"] | "")) dnsIp = gw;
    d.useStaticIp = 1;
    d.staticIp = (uint32_t)ip;
    d.gateway = (uint32_t)gw;
    d.subnet = (uint32_t)mask;
    d.dns = (uint32_t)dnsIp;
  } else {
    d.useStaticIp = 0;
  }
  s_->save();
  net_->requestJoin(ssid, pass);
  server.send(202, "application/json", "{}");
}

void Portal::handleWifiResult() {
  if (!requireAuth()) return;
  JsonDocument doc;
  switch (net_->joinState()) {
    case NetManager::Join::Joining: doc["state"] = "joining"; break;
    case NetManager::Join::Ok: doc["state"] = "ok"; break;
    case NetManager::Join::Failed: doc["state"] = "failed"; break;
    default: doc["state"] = "none"; break;
  }
  doc["ssid"] = net_->joinSsid();
  doc["error"] = net_->joinError();
  if (WiFi.status() == WL_CONNECTED) doc["ip"] = WiFi.localIP().toString();
  sendJson(200, doc);
}

// ---- device settings ----------------------------------------------------

void Portal::handleDeviceGet() {
  if (!requireAuth()) return;
  const SettingsData& d = s_->data();
  JsonDocument doc;
  doc["deviceId"] = d.deviceId;
  doc["tzOffsetSec"] = d.tzOffsetSec;
  doc["relayRfidMs"] = d.relayRfidMs;
  doc["relayFpMs"] = d.relayFpMs;
  doc["lanPortal"] = s_->flag(FLAG_LAN_PORTAL);
  doc["arduinoOta"] = s_->flag(FLAG_ARDUINO_OTA);
  doc["autoUpdate"] = s_->flag(FLAG_AUTO_UPDATE);
  doc["phyMode"] = d.phyMode;
  doc["txPowerDbm"] = d.txPowerQdBm / 4.0;
  doc["ssid"] = d.wifiSsid;
  JsonObject st = doc["staticIp"].to<JsonObject>();
  st["enabled"] = d.useStaticIp != 0;
  st["ip"] = IPAddress(d.staticIp).toString();
  st["gateway"] = IPAddress(d.gateway).toString();
  st["subnet"] = IPAddress(d.subnet).toString();
  st["dns"] = IPAddress(d.dns).toString();
  sendJson(200, doc);
}

void Portal::handleDevicePost() {
  if (!requireAuth()) return;
  JsonDocument in;
  if (!parseBody(in)) return;
  SettingsData& d = s_->data();
  bool changed = false;

  if (in["deviceId"].is<const char*>()) {
    String id = in["deviceId"].as<const char*>();
    id.trim();
    if (!Settings::validDeviceId(id)) return sendError(400, "Device ID: 1-64 printable characters");
    if (id != d.deviceId) {
      copyStr(d.deviceId, id);
      changed = true;
    }
  }
  if (in["tzOffsetSec"].is<long>()) {
    long tz = in["tzOffsetSec"];
    if (tz < -12 * 3600 || tz > 14 * 3600) return sendError(400, "Timezone out of range");
    if (tz != d.tzOffsetSec) {
      d.tzOffsetSec = tz;
      changed = true;
    }
  }
  if (in["relayRfidMs"].is<int>()) d.relayRfidMs = constrain(in["relayRfidMs"].as<int>(), 50, 10000);
  if (in["relayFpMs"].is<int>()) d.relayFpMs = constrain(in["relayFpMs"].as<int>(), 50, 10000);
  if (in["lanPortal"].is<bool>()) s_->setFlag(FLAG_LAN_PORTAL, in["lanPortal"]);
  if (in["arduinoOta"].is<bool>()) s_->setFlag(FLAG_ARDUINO_OTA, in["arduinoOta"]);
  if (in["autoUpdate"].is<bool>()) s_->setFlag(FLAG_AUTO_UPDATE, in["autoUpdate"]);
  if (in["phyMode"].is<int>()) d.phyMode = constrain(in["phyMode"].as<int>(), 0, 3);
  if (in["txPowerDbm"].is<float>()) d.txPowerQdBm = (uint8_t)constrain(in["txPowerDbm"].as<float>() * 4.0f, 0.0f, 82.0f);

  if (!s_->save()) return sendError(500, "Could not save settings");
  if (changed && hooks_.settingsChanged) hooks_.settingsChanged();
  server.send(200, "application/json", "{\"ok\":true}");
}

void Portal::handleLogs() {
  if (!requireAuth()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain", Log::dump());
}

// ---- security / maintenance ----------------------------------------------

void Portal::handlePassword() {
  if (!requireAuth()) return;
  JsonDocument in;
  if (!parseBody(in)) return;
  String current = in["current"] | "";
  // The master password also works here, to reset a forgotten admin password.
  if (!s_->checkAdminPassword(current) && !Settings::checkMasterPassword(current))
    return sendError(401, "Current password is wrong");
  if (!s_->setAdminPassword(in["next"] | "")) return sendError(400, "New password must be 8-64 characters");
  // Sign out every other session.
  const String& h = server.header("Authorization");
  for (Session& s : sessions_)
    if (s.token[0] && !h.endsWith(s.token)) s.token[0] = 0;
  LOGI("Admin password changed");
  server.send(200, "application/json", "{\"ok\":true}");
}

void Portal::handlePing() {
  if (!requireAuth()) return;
  JsonDocument doc;
  doc["ms"] = net_->online() ? CloudApi::pingServerMs() : -1;
  sendJson(200, doc);
}

void Portal::handleReboot() {
  if (!requireAuth()) return;
  rebootAt_ = millis() + 1000;
  server.send(200, "application/json", "{\"ok\":true}");
}

void Portal::handleResetWifi() {
  if (!requireAuth()) return;
  s_->clearWifi();
  LOGI("WiFi reset from portal");
  rebootAt_ = millis() + 1000;
  server.send(200, "application/json", "{\"ok\":true}");
}

void Portal::handleFactoryReset() {
  if (!requireAuth()) return;
  JsonDocument in;
  if (!parseBody(in)) return;
  if (String(in["confirm"] | "") != "RESET") return sendError(400, "Type RESET to confirm");
  LOGW("Factory reset from portal");
  s_->factoryReset();
  rebootAt_ = millis() + 1000;
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---- firmware from GitHub Releases --------------------------------------

void Portal::handleGithubStatus() {
  if (!requireAuth()) return;
  JsonDocument doc;
  doc["repo"] = GithubOta::repo();
  doc["signed"] = GithubOta::signedBuild();
  doc["state"] = gh_->stateName();
  doc["current"] = CHRM_FW_VERSION;
  doc["latest"] = gh_->latestVersion();
  doc["error"] = gh_->error();
  doc["lastCheckAgo"] = gh_->lastCheckAt() ? (millis() - gh_->lastCheckAt()) / 1000 : -1;
  doc["autoUpdate"] = s_->flag(FLAG_AUTO_UPDATE);
  doc["online"] = net_->online();
  sendJson(200, doc);
}

void Portal::handleGithubCheck() {
  if (!requireAuth()) return;
  if (gh_->state() == GithubOta::State::Disabled) return sendError(409, gh_->error());
  if (!net_->online()) return sendError(409, "Terminal is not connected to the internet");
  gh_->requestCheck();  // runs from the main loop; poll GET /api/github-ota
  server.send(202, "application/json", "{}");
}

void Portal::handleGithubInstall() {
  if (!requireAuth()) return;
  if (gh_->state() == GithubOta::State::Disabled) return sendError(409, gh_->error());
  if (!net_->online()) return sendError(409, "Terminal is not connected to the internet");
  if (hooks_.busy && hooks_.busy()) return sendError(409, "Terminal busy (enrollment running)");
  gh_->requestInstall();
  server.send(202, "application/json", "{}");
}

// ---- firmware upload ----------------------------------------------------

void Portal::handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadOk_ = allowed() && authorized() && !(hooks_.busy && hooks_.busy()) && ESP.getFreeHeap() > 12000;
    uploadBytes_ = 0;
    if (!uploadOk_) {
      LOGW("Firmware upload refused (auth, busy or low memory)");
      return;
    }
    LOGI("Firmware upload: %s", up.filename.c_str());
    WiFiUDP::stopAll();
    uint32_t maxSize = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSize, U_FLASH)) LOGE("Update.begin: %s", Update.getErrorString().c_str());
    ui_->progress("Updating", 0);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!uploadOk_ || Update.hasError()) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) LOGE("Update.write: %s", Update.getErrorString().c_str());
    uploadBytes_ += up.currentSize;
    // Total size is unknown during upload; the running image size is a close estimate.
    static int lastPct = -1;
    int pct = min<int>(99, uploadBytes_ * 100 / max<uint32_t>(ESP.getSketchSize(), 1));
    if (pct / 5 != lastPct / 5) ui_->progress("Updating", pct);
    lastPct = pct;
  } else if (up.status == UPLOAD_FILE_END) {
    if (!uploadOk_) return;
    if (!Update.end(true)) LOGE("Update.end: %s", Update.getErrorString().c_str());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (uploadOk_) Update.end();
    uploadOk_ = false;
    LOGW("Firmware upload aborted");
  }
  yield();
}

void Portal::handleUpdateDone() {
  if (!uploadOk_) {
    if (!authorized()) return sendError(401, "Login required");
    return sendError(409, "Device busy or low on memory; try again");
  }
  if (Update.hasError()) {
    String err = Update.getErrorString();
    ui_->text2("Update", "failed", cfg::RESULT_SCREEN_MS);
    JsonDocument doc;
    doc["error"] = err;
    return sendJson(500, doc);
  }
  LOGI("Firmware update OK (%u bytes), rebooting", (unsigned)uploadBytes_);
  ui_->text2("Update", "done", 0);
  JsonDocument doc;
  doc["ok"] = true;
  doc["bytes"] = uploadBytes_;
  doc["md5"] = Update.md5String();
  sendJson(200, doc);
  rebootAt_ = millis() + 1500;
}
