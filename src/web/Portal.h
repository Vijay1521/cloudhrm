// Device web portal: the setup page (gzipped in flash) and its JSON API.
// Served on the setup hotspot (192.168.4.1, with captive DNS) and, when
// enabled, on the office LAN. Every API call except /api/info, /api/setup and
// /api/login needs "Authorization: Bearer <token>".
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <functional>

class Settings;
class NetManager;
class Ui;
class GithubOta;

struct PortalHooks {
  std::function<void(JsonObject)> fillStatus;  // everything on the Status tab
  std::function<bool()> busy;                  // true while an enrollment runs
  std::function<void()> settingsChanged;       // device ID / timezone edited
};

class Portal {
 public:
  void begin(Settings& settings, NetManager& net, Ui& ui, GithubOta& githubOta, PortalHooks hooks);
  void loop();

 private:
  struct Session {
    char token[33];
    uint32_t lastUsed;
  };

  void routes();
  bool viaHotspot();
  bool allowed();
  bool authorized();
  bool requireAuth();
  bool parseBody(JsonDocument& doc);
  void sendJson(int code, const JsonDocument& doc);
  void sendError(int code, const char* msg);
  String newSession();

  void handleIndex();
  void handleInfo();
  void handleSetup();
  void handleLogin();
  void handleLogout();
  void handleStatus();
  void handleScanStart();
  void handleScanResult();
  void handleWifi();
  void handleWifiResult();
  void handleDeviceGet();
  void handleDevicePost();
  void handleLogs();
  void handlePassword();
  void handleApPassword();
  void handlePing();
  void handleReboot();
  void handleResetWifi();
  void handleFactoryReset();
  void handleUpdateDone();
  void handleUpdateUpload();
  void handleGithubStatus();
  void handleGithubCheck();
  void handleGithubInstall();
  void handleNotFound();

  Settings* s_ = nullptr;
  NetManager* net_ = nullptr;
  Ui* ui_ = nullptr;
  GithubOta* gh_ = nullptr;
  PortalHooks hooks_;
  bool dnsOn_ = false;
  Session sessions_[3] = {};
  uint8_t loginFails_ = 0;
  uint32_t lockedUntil_ = 0;
  uint32_t rebootAt_ = 0;
  bool uploadOk_ = false;
  size_t uploadBytes_ = 0;
};
