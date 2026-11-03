// The six CloudHRM endpoints, with the same URLs, fields and field order as
// v1. Each call reads the response body exactly once.
#pragma once

#include <Arduino.h>

class Settings;

struct ApiResult {
  int http = 0;          // HTTP status, or a negative HTTPClient error
  String message;        // "message"
  String fullName;       // "data.full_name"
  long biometricId = 0;  // "data.biometric_id" (get-biometric-id only)
  uint32_t ms = 0;       // round-trip time

  bool ok() const { return http == 200 || http == 201; }
  bool networkError() const { return http <= 0 || http >= 500; }
};

class CloudApi {
 public:
  void begin(Settings& settings);

  ApiResult markAttendance(const String& accessId, char accessType);  // 'B' fingerprint, 'R' card
  ApiResult getBiometricId(const String& employeeId, const String& deviceType);
  ApiResult registerBiometric(const String& employeeId, long biometricId, const String& deviceType);
  ApiResult registerRfid(const String& employeeId, const String& deviceType, const String& rfidId);
  ApiResult updateDeviceConfig(const String& macAddress);

  // Last result of any call, for the portal.
  int lastHttp() const { return lastHttp_; }
  uint32_t lastMs() const { return lastMs_; }
  uint32_t lastAt() const { return lastAt_; }

  // TCP reachability of the API host (no HTTP request is sent).
  static int32_t pingServerMs();

 private:
  ApiResult post(const char* url, const String& body);
  void prepareTls();

  Settings* s_ = nullptr;
  bool mflnChecked_ = false;
  bool mfln_ = false;
  int lastHttp_ = 0;
  uint32_t lastMs_ = 0;
  uint32_t lastAt_ = 0;
};
