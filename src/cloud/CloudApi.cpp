#include "CloudApi.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>

#include "../config.h"
#include "../core/Logger.h"
#include "../core/Settings.h"
#include "FormEncoder.h"

#if CHRM_TLS_MODE == CHRM_TLS_VERIFY
#include "TrustAnchors.h"
#endif

namespace {

BearSSL::WiFiClientSecure tls;

String deviceId(Settings* s) { return String(s->data().deviceId); }

}  // namespace

void CloudApi::begin(Settings& settings) {
  s_ = &settings;
#if CHRM_TLS_MODE == CHRM_TLS_VERIFY
  tls.setTrustAnchors(TrustAnchors::get());
#else
  tls.setInsecure();  // same as v1
#endif
  tls.setTimeout(cfg::HTTP_TIMEOUT_MS);
}

void CloudApi::prepareTls() {
  if (!mflnChecked_) {
    // Smaller TLS buffers save ~15 KB heap if the server supports it.
    // This is only a TLS handshake probe; no HTTP request is made.
    mflnChecked_ = true;
    mfln_ = BearSSL::WiFiClientSecure::probeMaxFragmentLength(cfg::API_HOST, cfg::API_PORT, 1024);
    LOGI("TLS max fragment length 1024: %s", mfln_ ? "supported" : "not supported");
  }
  if (mfln_) tls.setBufferSizes(1024, 1024);
#if CHRM_TLS_MODE == CHRM_TLS_VERIFY
  tls.setX509Time(time(nullptr));
#endif
}

ApiResult CloudApi::post(const char* url, const String& body) {
  ApiResult r;
#if CHRM_TLS_MODE == CHRM_TLS_VERIFY
  if (time(nullptr) < 1700000000) {
    LOGW("Time not synced; certificate check impossible");
    r.http = -100;
    lastHttp_ = r.http;
    return r;
  }
#endif
  prepareTls();
  uint32_t t0 = millis();

  for (int attempt = 0; attempt < 2; attempt++) {
    HTTPClient http;
    http.setTimeout(cfg::HTTP_TIMEOUT_MS);
    if (!http.begin(tls, url)) {
      r.http = HTTPC_ERROR_CONNECTION_FAILED;
      break;
    }
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");  // as in v1
    r.http = http.POST(body);
    String payload = r.http > 0 ? http.getString() : String();
    http.end();
    // The request still says "Connection: keep-alive" like v1, but close the
    // socket so the ~22 KB of TLS buffers are free for the portal and updates.
    tls.stop();

    // Retry only if the connection failed before anything was sent, so a
    // punch can never be logged twice.
    if (r.http == HTTPC_ERROR_CONNECTION_FAILED && attempt == 0) {
      LOGW("Connect failed, retrying once");
      delay(500);
      continue;
    }

    if (payload.length()) {
      JsonDocument doc;
      if (deserializeJson(doc, payload) == DeserializationError::Ok) {
        r.message = doc["message"] | "";
        JsonVariant data = doc["data"];
        r.fullName = data["full_name"] | "";
        JsonVariant bio = data["biometric_id"];
        r.biometricId = bio.is<const char*>() ? atol(bio.as<const char*>()) : bio.as<long>();
      }
    }
    break;
  }

  r.ms = millis() - t0;
  lastHttp_ = r.http;
  lastMs_ = r.ms;
  lastAt_ = millis();
  if (r.http > 0)
    LOGI("POST %s -> %d (%lums)", strrchr(url, '/'), r.http, (unsigned long)r.ms);
  else
    LOGW("POST %s failed: %s", strrchr(url, '/'), HTTPClient::errorToString(r.http).c_str());
  return r;
}

ApiResult CloudApi::markAttendance(const String& accessId, char accessType) {
  FormEncoder f;
  f.add("device_id", deviceId(s_))
      .add("device_type", "0")
      .add("access_id", accessId)
      .add("access_type", String(accessType));
  return post(cfg::URL_ACCESS_LOG, f.str());
}

ApiResult CloudApi::getBiometricId(const String& employeeId, const String& deviceType) {
  FormEncoder f;
  f.add("device_id", deviceId(s_)).add("device_type", deviceType).add("employee_id", employeeId);
  return post(cfg::URL_GET_BIOMETRIC_ID, f.str());
}

ApiResult CloudApi::registerBiometric(const String& employeeId, long biometricId, const String& deviceType) {
  FormEncoder f;
  f.add("device_id", deviceId(s_))
      .add("employee_id", employeeId)
      .add("biometric_id", String(biometricId))
      .add("device_type", deviceType);
  return post(cfg::URL_REGISTER_BIOMETRIC, f.str());
}

ApiResult CloudApi::registerRfid(const String& employeeId, const String& deviceType, const String& rfidId) {
  FormEncoder f;
  f.add("device_id", deviceId(s_))
      .add("device_type", deviceType)
      .add("employee_id", employeeId)
      .add("rfid_id", rfidId);
  return post(cfg::URL_REGISTER_RFID, f.str());
}

ApiResult CloudApi::updateDeviceConfig(const String& macAddress) {
  FormEncoder f;
  f.add("device_id", deviceId(s_)).add("device_mac_address", macAddress);
  return post(cfg::URL_DEVICE_CONFIG, f.str());
}

int32_t CloudApi::pingServerMs() {
  WiFiClient c;
  c.setTimeout(5000);
  uint32_t t0 = millis();
  bool ok = c.connect(cfg::API_HOST, cfg::API_PORT);
  int32_t ms = ok ? (int32_t)(millis() - t0) : -1;
  c.stop();
  return ms;
}
