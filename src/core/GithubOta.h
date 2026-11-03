// Firmware updates pulled from GitHub Releases.
//
// The release workflow publishes a signed firmware file plus manifest.json:
//   {"version":"2.0.1","tag":"v2.0.1","bin":"CloudHRM_v2.bin","size":605792,"md5":"..."}
// The device reads .../releases/latest/download/manifest.json, and if the
// version is newer it downloads the file from the same repository and tag.
// Both connections verify GitHub's certificate. The core's Updater then
// rejects any image not signed with the key matching public.key.
// The CloudHRM server is never contacted by this module.
#pragma once

#include <Arduino.h>

#include <functional>

class Settings;
class Ui;

class GithubOta {
 public:
  enum class State { Disabled, Idle, Checking, UpToDate, Available, Installing, Failed };

  void begin(Settings& settings, Ui& ui);
  // online: WiFi is up. idle: no enrollment running. quiet: no punch for a
  // while (required for the unattended nightly install).
  void loop(bool online, bool idle, bool quiet);

  // Called right before a download starts (e.g. to drop MQTT and free RAM).
  void onBeforeInstall(std::function<void()> f) { beforeInstall_ = f; }

  void requestCheck() { wantCheck_ = true; }
  void requestInstall() { wantInstall_ = true; }

  State state() const { return state_; }
  const char* stateName() const;
  const String& latestVersion() const { return latest_; }
  const char* error() const { return error_.c_str(); }
  uint32_t lastCheckAt() const { return lastCheck_; }
  static bool configured();
  static bool signedBuild();
  static String repo();

  // "2.0.10" > "2.0.9"
  static int compareVersions(const String& a, const String& b);

 private:
  bool checkNow();
  void installNow();
  bool autoWindow();
  void fail(const String& why);

  Settings* s_ = nullptr;
  Ui* ui_ = nullptr;
  std::function<void()> beforeInstall_;
  State state_ = State::Disabled;
  bool wantCheck_ = false;
  bool wantInstall_ = false;
  String latest_, tag_, bin_, md5_;
  uint32_t size_ = 0;
  String error_;
  uint32_t lastCheck_ = 0;
  int lastAutoDay_ = -1;
  uint32_t rebootAt_ = 0;
};
