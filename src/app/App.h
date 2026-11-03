// Application layer: owns every module, runs the cooperative main loop,
// attendance punches and the remote (MQTT) jobs.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../cloud/CloudApi.h"
#include "../cloud/MqttLink.h"
#include "../core/BootCounter.h"
#include "../core/GithubOta.h"
#include "../core/Ota.h"
#include "../core/Settings.h"
#include "../core/TimeSvc.h"
#include "../devices/Biometric.h"
#include "../devices/Relay.h"
#include "../devices/RfidReader.h"
#include "../net/NetManager.h"
#include "../ui/Ui.h"
#include "../web/Portal.h"
#include "JobQueue.h"

class App {
 public:
  void begin();
  void loop();

 private:
  enum class JobPhase { None, FingerEnroll, CardWait };

  struct Punch {
    uint32_t at;
    char type;
    String id;
    int http;
  };

  void handleResetGesture(uint8_t resets);
  void registerDevice();
  void attendance();
  void punch(const String& accessId, char type);
  void showResult(const ApiResult& r, uint16_t relayMs);
  void runJobs();
  void startJob(const Job& job);
  void finishFingerEnroll(bool ok);
  void drawIdle();
  void watchJoinResult();
  void fillStatus(JsonObject o);
  void onSettingsChanged();

  Settings settings_;
  BootCounter boot_;
  TimeSvc time_;
  Ui ui_;
  Relay relay_;
  RfidReader rfid_;
  Biometric bio_;
  NetManager net_;
  CloudApi api_;
  MqttLink mqtt_;
  JobQueue jobs_;
  Portal portal_;
  Ota ota_;
  GithubOta githubOta_;
  uint32_t lastPunchAt_ = 0;

  // remote jobs
  JobPhase phase_ = JobPhase::None;
  Job job_;
  uint16_t enrollSlot_ = 0;
  uint32_t jobDeadline_ = 0;

  // device registration (update-device-configuration-status), once per boot
  bool registered_ = false;
  uint32_t nextRegTry_ = 0;
  uint32_t regBackoff_ = 10000;

  // punch debounce and history
  String lastCard_;
  uint32_t lastCardAt_ = 0;
  uint16_t lastFinger_ = 0;
  uint32_t lastFingerAt_ = 0;
  Punch history_[5];
  uint8_t historyCount_ = 0;

  uint32_t lastIdleDraw_ = 0;
  NetManager::Join lastJoin_ = NetManager::Join::None;
  uint16_t fpTemplates_ = 0;
  uint32_t fpTemplatesAt_ = 0;
};
