// Persistent device settings, stored as one CRC-checked record in
// LittleFS (/cfg.bin). Saves are atomic: write /cfg.tmp, then rename.
#pragma once

#include <Arduino.h>

enum class PhyMode : uint8_t { Auto = 0, B = 1, G = 2, N = 3 };

enum SettingsFlags : uint32_t {
  FLAG_LAN_PORTAL = 1u << 0,   // serve the portal on the office LAN
  FLAG_ARDUINO_OTA = 1u << 1,  // accept ArduinoOTA uploads on the LAN
  FLAG_AUTO_UPDATE = 1u << 2,  // install new GitHub releases at night
};

struct SettingsData {
  uint32_t magic;
  uint16_t schema;
  uint16_t size;

  char wifiSsid[33];
  char wifiPass[65];
  char deviceId[65];
  int32_t tzOffsetSec;

  uint8_t useStaticIp;
  uint32_t staticIp, gateway, subnet, dns;

  uint8_t phyMode;       // PhyMode
  uint8_t lastGoodPhy;   // PhyMode that last joined successfully (Auto = none yet)
  uint8_t txPowerQdBm;   // quarter dB, 82 = 20.5 dBm

  char apPass[17];       // unused since 2.0.2 (hotspot password is fixed in secrets.h); kept for the file layout
  uint8_t adminSet;
  uint8_t adminSalt[16];
  uint8_t adminHash[32];  // SHA-256(salt + password)
  char otaMd5[33];        // MD5(password) hex, for ArduinoOTA

  uint16_t relayRfidMs;
  uint16_t relayFpMs;
  uint32_t flags;

  uint32_t crc;
};

class Settings {
 public:
  // Mounts LittleFS and loads the record (defaults if missing or corrupt).
  bool begin();
  SettingsData& data() { return d_; }
  const SettingsData& data() const { return d_; }

  // Writes only if something changed since the last save.
  bool save();
  bool storageOk() const { return fsOk_; }

  bool hasWifi() const { return d_.wifiSsid[0] != 0; }
  bool hasDeviceId() const { return d_.deviceId[0] != 0; }
  bool flag(uint32_t f) const { return (d_.flags & f) != 0; }
  void setFlag(uint32_t f, bool on);

  void clearWifi();      // 4-reset gesture / portal "reset WiFi"
  void factoryReset();   // everything back to defaults (new AP password too)

  bool setAdminPassword(const String& pw);
  bool checkAdminPassword(const String& pw) const;
  static bool checkMasterPassword(const String& pw);

  static bool validDeviceId(const String& id);
  static bool validSsid(const String& s) { return s.length() >= 1 && s.length() <= 32; }
  static bool validWifiPass(const String& p) { return p.length() == 0 || (p.length() >= 8 && p.length() <= 63); }

 private:
  void setDefaults();
  static uint32_t crc32(const uint8_t* p, size_t n);
  uint32_t computeCrc() const;

  SettingsData d_{};
  SettingsData saved_{};
  bool fsOk_ = false;
};

// Copies src into a fixed char array, always NUL-terminated.
template <size_t N>
void copyStr(char (&dst)[N], const String& src) {
  strncpy(dst, src.c_str(), N - 1);
  dst[N - 1] = 0;
}
