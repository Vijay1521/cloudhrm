#include "RfidReader.h"

#include <SPI.h>

#include "../core/Logger.h"

void RfidReader::begin(uint8_t ssPin, uint8_t rstPin) {
  SPI.begin();
  mfrc_ = new MFRC522(ssPin, rstPin);
  mfrc_->PCD_Init();
  version_ = mfrc_->PCD_ReadRegister(MFRC522::VersionReg);
  if (version_ == 0x00 || version_ == 0xFF)
    LOGE("RC522 not responding (version 0x%02X)", version_);
  else
    LOGI("RC522 version 0x%02X", version_);
}

void RfidReader::checkHealth() {
  // The RC522 occasionally locks up after electrical noise; re-init if so.
  version_ = mfrc_->PCD_ReadRegister(MFRC522::VersionReg);
  if (version_ == 0x00 || version_ == 0xFF) {
    LOGW("RC522 lost, re-initialising");
    mfrc_->PCD_Init();
    version_ = mfrc_->PCD_ReadRegister(MFRC522::VersionReg);
  }
}

bool RfidReader::poll(String& uid) {
  uint32_t now = millis();
  if (now - lastPoll_ < 50) return false;
  lastPoll_ = now;
  if (now - lastHealth_ > 60000) {
    lastHealth_ = now;
    checkHealth();
  }
  if (!mfrc_->PICC_IsNewCardPresent() || !mfrc_->PICC_ReadCardSerial()) return false;

  uid = "";
  for (byte i = 0; i < mfrc_->uid.size; i++) uid += String(mfrc_->uid.uidByte[i], HEX);
  mfrc_->PICC_HaltA();
  mfrc_->PCD_StopCrypto1();
  return true;
}
