#include "Settings.h"

#include <LittleFS.h>
#include <MD5Builder.h>
#include <bearssl/bearssl.h>

#include "../config.h"
#include "Logger.h"

namespace {

constexpr uint32_t kMagic = 0x4D524843;  // "CHRM"
constexpr uint16_t kSchema = 1;
const char kFile[] = "/cfg.bin";
const char kTmp[] = "/cfg.tmp";

void hashPassword(const uint8_t salt[16], const String& pw, uint8_t out[32]) {
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, salt, 16);
  br_sha256_update(&ctx, pw.c_str(), pw.length());
  br_sha256_out(&ctx, out);
}

}  // namespace

bool Settings::begin() {
  fsOk_ = LittleFS.begin();
  if (!fsOk_) {
    LOGE("LittleFS mount failed; check Flash size setting (needs an FS partition)");
    setDefaults();
    saved_ = d_;
    return false;
  }
  bool loaded = false;
  File f = LittleFS.open(kFile, "r");
  if (f) {
    SettingsData tmp{};
    if (f.size() == sizeof(tmp) && f.read(reinterpret_cast<uint8_t*>(&tmp), sizeof(tmp)) == sizeof(tmp)) {
      d_ = tmp;
      loaded = d_.magic == kMagic && d_.schema == kSchema && d_.size == sizeof(SettingsData) &&
               d_.crc == computeCrc();
    }
    f.close();
  }
  if (!loaded) {
    LOGW("No valid settings, using defaults");
    setDefaults();
    saved_ = SettingsData{};  // force the first save
    save();
  } else {
    saved_ = d_;
  }
  return true;
}

void Settings::setDefaults() {
  d_ = SettingsData{};
  d_.magic = kMagic;
  d_.schema = kSchema;
  d_.size = sizeof(SettingsData);
  d_.tzOffsetSec = cfg::DEFAULT_TZ_OFFSET_SEC;
  d_.phyMode = (uint8_t)PhyMode::Auto;
  d_.lastGoodPhy = (uint8_t)PhyMode::Auto;
  d_.txPowerQdBm = 82;
  d_.relayRfidMs = cfg::DEFAULT_RELAY_RFID_MS;
  d_.relayFpMs = cfg::DEFAULT_RELAY_FP_MS;
  d_.flags = FLAG_LAN_PORTAL | FLAG_ARDUINO_OTA | FLAG_AUTO_UPDATE;
  regenerateApPassword();
}

bool Settings::save() {
  d_.magic = kMagic;
  d_.schema = kSchema;
  d_.size = sizeof(SettingsData);
  d_.crc = computeCrc();
  if (memcmp(&d_, &saved_, sizeof(d_)) == 0) return true;
  if (!fsOk_) return false;

  File f = LittleFS.open(kTmp, "w");
  if (!f) {
    LOGE("Settings: cannot open temp file");
    return false;
  }
  size_t n = f.write(reinterpret_cast<const uint8_t*>(&d_), sizeof(d_));
  f.close();
  if (n != sizeof(d_)) {
    LOGE("Settings: short write");
    LittleFS.remove(kTmp);
    return false;
  }
  if (!LittleFS.rename(kTmp, kFile)) {
    LittleFS.remove(kFile);
    if (!LittleFS.rename(kTmp, kFile)) {
      LOGE("Settings: rename failed");
      return false;
    }
  }
  saved_ = d_;
  LOGI("Settings saved");
  return true;
}

void Settings::setFlag(uint32_t f, bool on) {
  if (on)
    d_.flags |= f;
  else
    d_.flags &= ~f;
}

void Settings::clearWifi() {
  memset(d_.wifiSsid, 0, sizeof(d_.wifiSsid));
  memset(d_.wifiPass, 0, sizeof(d_.wifiPass));
  d_.useStaticIp = 0;
  d_.lastGoodPhy = (uint8_t)PhyMode::Auto;
  save();
}

void Settings::factoryReset() {
  setDefaults();
  save();
}

bool Settings::setAdminPassword(const String& pw) {
  if (pw.length() < 8 || pw.length() > 64) return false;
  ESP.random(d_.adminSalt, sizeof(d_.adminSalt));
  hashPassword(d_.adminSalt, pw, d_.adminHash);
  MD5Builder md5;
  md5.begin();
  md5.add(pw);
  md5.calculate();
  copyStr(d_.otaMd5, md5.toString());
  d_.adminSet = 1;
  return save();
}

bool Settings::checkAdminPassword(const String& pw) const {
  if (!d_.adminSet) return false;
  uint8_t h[32];
  hashPassword(d_.adminSalt, pw, h);
  uint8_t diff = 0;  // constant-time compare
  for (size_t i = 0; i < sizeof(h); i++) diff |= h[i] ^ d_.adminHash[i];
  return diff == 0;
}

void Settings::regenerateApPassword() {
  static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";  // no 0/o/1/l/i
  for (size_t i = 0; i < 10; i++) d_.apPass[i] = alphabet[ESP.random() % (sizeof(alphabet) - 1)];
  d_.apPass[10] = 0;
}

bool Settings::validDeviceId(const String& id) {
  if (id.length() < 1 || id.length() > 64) return false;
  for (size_t i = 0; i < id.length(); i++) {
    char c = id[i];
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

uint32_t Settings::crc32(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFF;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
  }
  return ~crc;
}

uint32_t Settings::computeCrc() const {
  return crc32(reinterpret_cast<const uint8_t*>(&d_), offsetof(SettingsData, crc));
}
