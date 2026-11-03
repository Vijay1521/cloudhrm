#include "MqttLink.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "../app/JobQueue.h"
#include "../config.h"
#include "../core/Logger.h"
#include "../core/Settings.h"

namespace {

WiFiClient net;
PubSubClient mqtt(net);

// v1 read every field into a String, so JSON null became "null".
bool isNullish(JsonVariant v) {
  if (v.isNull()) return true;
  const char* s = v.as<const char*>();
  return s && (strcmp(s, "null") == 0 || s[0] == 0);
}

String text(JsonVariant v) {
  if (v.isNull()) return String();
  if (v.is<const char*>()) return String(v.as<const char*>());
  String out;
  serializeJson(v, out);  // numbers, e.g. "emp_id": 117
  return out;
}

}  // namespace

void MqttLink::begin(Settings& settings, JobQueue& jobs) {
  s_ = &settings;
  jobs_ = &jobs;
  mqtt.setServer(cfg::MQTT_HOST, cfg::MQTT_PORT);
  mqtt.setBufferSize(cfg::MQTT_BUFFER_SIZE);
  mqtt.setKeepAlive(cfg::MQTT_KEEPALIVE_SEC);
  mqtt.setSocketTimeout(cfg::MQTT_SOCKET_TIMEOUT_SEC);
  mqtt.setCallback([this](char* t, uint8_t* p, unsigned int n) { onMessage(t, p, n); });
  net.setTimeout(5000);
}

bool MqttLink::connected() const { return mqtt.connected(); }
int MqttLink::state() const { return mqtt.state(); }

void MqttLink::disconnect() {
  if (mqtt.connected()) mqtt.disconnect();
}

void MqttLink::loop() {
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if ((int32_t)(millis() - nextTry_) >= 0) tryConnect();
}

void MqttLink::tryConnect() {
  if (!s_->hasDeviceId()) return;  // the topic is the device ID
  String clientId = String(ESP.getChipId());  // as in v1
  topic_ = s_->data().deviceId;
  if (mqtt.connect(clientId.c_str(), cfg::MQTT_USER, cfg::MQTT_PASSWORD)) {
    mqtt.subscribe(topic_.c_str());
    failures_ = 0;
    LOGI("MQTT connected, topic %s", topic_.c_str());
  } else {
    failures_++;
    uint32_t wait = min<uint32_t>(5000UL * failures_, 60000UL);
    nextTry_ = millis() + wait;
    LOGW("MQTT connect failed (rc=%d), retry in %lus", mqtt.state(), (unsigned long)(wait / 1000));
  }
}

void MqttLink::onMessage(char* /*topic*/, uint8_t* payload, unsigned int length) {
  lastMsgAt_ = millis();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    LOGW("MQTT: bad JSON (%s)", err.c_str());
    return;
  }
  String device = text(doc["device"]);
  if (device != s_->data().deviceId) {
    LOGI("MQTT: message for another device ignored");
    return;
  }

  JsonVariant regType = doc["reg_type"];
  Job job;
  job.employeeId = text(doc["emp_id"]);
  job.deviceType = text(doc["device_type"]);

  if (!isNullish(regType)) {
    String rt = text(regType);
    if (rt == "B")
      job.type = JobType::EnrollFinger;
    else if (rt == "R")
      job.type = JobType::EnrollCard;
    else {
      LOGW("MQTT: unknown reg_type '%s'", rt.c_str());
      return;
    }
  } else if (!isNullish(doc["RTC_update"])) {
    job.type = JobType::SetTimezone;
    job.value = text(doc["RTC_update"]);
  } else {
    return;
  }

  if (jobs_->push(job))
    LOGI("MQTT: job queued (type %u, emp %s)", (unsigned)job.type, job.employeeId.c_str());
  else
    LOGW("MQTT: job queue full, command dropped");
}
