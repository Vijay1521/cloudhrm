// WiFi station management tuned for weak signal, plus provisioning
// (SmartConfig and the setup hotspot, alternating). Never blocks for long.
#pragma once

#include <Arduino.h>

class Settings;

class NetManager {
 public:
  enum class State { Idle, Connecting, Online, Offline, Provisioning };
  enum class Phase { SmartConfig, AccessPoint };
  enum class Join { None, Joining, Ok, Failed };

  void begin(Settings& settings);
  void loop();

  void connect();                       // station mode with saved credentials
  void startProvisioning(bool canGiveUp);

  State state() const { return state_; }
  bool online() const { return state_ == State::Online; }
  Phase phase() const { return phase_; }
  uint32_t phaseSecsLeft() const;
  bool apActive() const { return apOn_; }
  bool apClientConnected() const;
  static String apSsid();
  static String hostname();

  // Test-joins new credentials (portal or SmartConfig). On success they are
  // saved and the device switches to normal station mode.
  void requestJoin(const String& ssid, const String& pass);
  Join joinState() const { return join_; }
  const char* joinError() const { return joinError_; }
  const String& joinSsid() const { return joinSsid_; }

  // Async scan for the portal site survey: -1 running, -2 idle, >=0 results.
  void startScan();
  int scanResult() const;

  int32_t rssi() const;
  const char* phyName() const;
  const char* lastError() const { return lastError_; }
  uint32_t offlineSinceMs() const { return offlineSince_; }
  // RSSI samples, one per minute, oldest first.
  uint8_t rssiHistory(int8_t* out, uint8_t max) const;

 private:
  void startAttempt();
  void applyRadioTuning();
  uint8_t currentPhy() const;
  void enterPhase(Phase p);
  void stopProvisioning();
  void pollJoin();
  void pollRoaming();
  void sampleRssi();
  static const char* statusText(int status);

  Settings* s_ = nullptr;
  State state_ = State::Idle;
  Phase phase_ = Phase::SmartConfig;
  bool apOn_ = false;
  bool canGiveUp_ = false;
  uint32_t provStart_ = 0;
  uint32_t phaseEnd_ = 0;

  Join join_ = Join::None;
  bool joinFromSc_ = false;
  String joinSsid_, joinPass_, joinDeviceId_;
  uint32_t joinDeadline_ = 0;
  uint32_t joinOkAt_ = 0;
  const char* joinError_ = "";

  uint8_t tryPhy_ = 0;       // PhyMode used for the current attempt
  uint8_t failsOnPhy_ = 0;
  uint8_t backoffStep_ = 0;
  uint32_t attemptDeadline_ = 0;
  uint32_t retryAt_ = 0;
  uint32_t lostAt_ = 0;
  uint32_t offlineSince_ = 0;
  const char* lastError_ = "";

  bool scanForPortal_ = false;
  bool roamScan_ = false;
  uint32_t lastRoamScan_ = 0;
  uint8_t weakSamples_ = 0;

  int8_t hist_[10] = {};
  uint8_t histCount_ = 0;
  uint32_t lastSample_ = 0;
};
