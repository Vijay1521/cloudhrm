// CloudHRM terminal firmware v2 - build-wide configuration.
//
// Everything the CloudHRM backend sees (URLs, MQTT settings) is copied
// verbatim from v1. Do not edit those values: the server must not be able to
// tell a v2 device from a v1 device.
#pragma once

#include <Arduino.h>

#define CHRM_FW_VERSION "2.0.1"

// 1 = print logs on Serial at 115200. The fingerprint sensor shares this UART,
// so the sensor is disabled in such builds. Keep 0 for production.
#ifndef CHRM_SERIAL_DEBUG
#define CHRM_SERIAL_DEBUG 0
#endif

// TLS certificate handling for the CloudHRM API.
//   CHRM_TLS_INSECURE: no certificate check (same as v1).
//   CHRM_TLS_VERIFY:   check against the root CAs in src/cloud/trust_anchors.h.
//                      Needs working NTP; punches fail while time is not synced.
#define CHRM_TLS_INSECURE 0
#define CHRM_TLS_VERIFY 1
#ifndef CHRM_TLS_MODE
#define CHRM_TLS_MODE CHRM_TLS_INSECURE
#endif

namespace cfg {

// ---- CloudHRM API (verbatim from v1) ---------------------------------------
constexpr char URL_DEVICE_CONFIG[] =   // v1: deviceconfig
    "https://login.cloudhrm.in/api/devices/access-control/update-device-configuration-status";
constexpr char URL_GET_BIOMETRIC_ID[] =  // v1: linkcheck
    "https://login.cloudhrm.in/api/devices/access-control/get-biometric-id";
constexpr char URL_REGISTER_BIOMETRIC[] =  // v1: linkreg
    "https://login.cloudhrm.in/api/devices/access-control/register-biometric-id";
constexpr char URL_ACCESS_LOG[] =  // v1: linkmark
    "https://login.cloudhrm.in/api/devices/access-control/create-device-access-log";
constexpr char URL_REGISTER_RFID[] =  // v1: markrfid
    "https://login.cloudhrm.in/api/devices/access-control/register-rfid-id";
// v1: serverName. Declared in v1 but never called; kept unchanged for reference.
constexpr char URL_LEGACY_SET_DEVICE[] = "http://yourdata.in/dr_bath/api/device/setdevice";

constexpr char API_HOST[] = "login.cloudhrm.in";
constexpr uint16_t API_PORT = 443;

// ---- MQTT (verbatim from v1) -----------------------------------------------
constexpr char MQTT_HOST[] = "iot.efficientindia.in";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USER[] = "cloudhrm";
constexpr char MQTT_PASSWORD[] = "Cloud#hrm";
// Client ID = ESP.getChipId() in decimal, topic = device ID (both as in v1).

constexpr char NTP_SERVER[] = "asia.pool.ntp.org";

// ---- Firmware updates from GitHub Releases ---------------------------------
// "owner/repo" whose Releases hold the signed firmware built by
// .github/workflows/firmware-release.yml. Empty = GitHub updates switched off.
// The device only installs images signed with the key matching public.key.
constexpr char GITHUB_OTA_REPO[] = "Vijay1521/cloudhrm";
constexpr char GITHUB_OTA_MANIFEST[] = "manifest.json";
constexpr uint8_t AUTO_UPDATE_HOUR = 2;            // nightly window 02:00-04:00 local time
constexpr uint32_t AUTO_UPDATE_IDLE_MS = 600000;   // no punch for 10 min before auto-install

// ---- Pins (same wiring as v1) ----------------------------------------------
constexpr uint8_t PIN_RFID_SS = D4;   // GPIO2
constexpr uint8_t PIN_RFID_RST = D3;  // GPIO0
constexpr uint8_t PIN_RELAY = 16;     // D0
constexpr uint8_t OLED_I2C_ADDR = 0x3C;
constexpr uint32_t FINGERPRINT_BAUD = 57600;

// ---- Behaviour -------------------------------------------------------------
constexpr int32_t DEFAULT_TZ_OFFSET_SEC = 19800;  // IST, until the server sends RTC_update
constexpr uint16_t DEFAULT_RELAY_RFID_MS = 500;   // v1 values
constexpr uint16_t DEFAULT_RELAY_FP_MS = 1000;

constexpr uint32_t HTTP_TIMEOUT_MS = 12000;
constexpr uint16_t MQTT_KEEPALIVE_SEC = 60;
constexpr uint16_t MQTT_SOCKET_TIMEOUT_SEC = 15;
constexpr uint16_t MQTT_BUFFER_SIZE = 512;

constexpr uint32_t WIFI_JOIN_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_TEST_TIMEOUT_MS = 25000;  // credentials from portal / SmartConfig
constexpr uint32_t SMARTCONFIG_WINDOW_MS = 120000;
constexpr uint32_t AP_WINDOW_MS = 180000;
constexpr uint32_t PROVISIONING_GIVE_UP_MS = 300000;  // only when saved WiFi exists
constexpr int8_t ROAM_RSSI_THRESHOLD = -80;

constexpr uint32_t PUNCH_DEBOUNCE_MS = 5000;  // same card / finger ignored for this long
constexpr uint32_t RESULT_SCREEN_MS = 2000;
constexpr uint32_t ENROLL_STEP_TIMEOUT_MS = 20000;
constexpr uint32_t RFID_ENROLL_WINDOW_MS = 15000;

constexpr uint32_t BOOT_COUNT_WINDOW_MS = 3000;
constexpr uint8_t RESETS_PROVISION = 2;  // v1 gesture
constexpr uint8_t RESETS_WIFI_WIPE = 4;  // v1 gesture
constexpr uint8_t RESETS_FACTORY = 6;    // new: full factory reset

constexpr uint32_t SESSION_IDLE_MS = 30UL * 60UL * 1000UL;
constexpr uint8_t LOGIN_MAX_FAILS = 5;
constexpr uint32_t LOGIN_LOCKOUT_MS = 5UL * 60UL * 1000UL;

}  // namespace cfg
