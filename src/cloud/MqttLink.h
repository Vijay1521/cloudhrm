// Command channel from the CloudHRM server (enrollment, timezone).
// Broker, credentials, client ID (chip ID) and topic (device ID) are as in v1.
#pragma once

#include <Arduino.h>

class Settings;
class JobQueue;

class MqttLink {
 public:
  void begin(Settings& settings, JobQueue& jobs);
  // Call every loop while WiFi is online; reconnects with backoff.
  void loop();
  void disconnect();
  bool connected() const;
  int state() const;
  uint32_t lastMessageAt() const { return lastMsgAt_; }

 private:
  void tryConnect();
  void onMessage(char* topic, uint8_t* payload, unsigned int length);

  Settings* s_ = nullptr;
  JobQueue* jobs_ = nullptr;
  uint32_t nextTry_ = 0;
  uint8_t failures_ = 0;
  uint32_t lastMsgAt_ = 0;
  String topic_;
};
