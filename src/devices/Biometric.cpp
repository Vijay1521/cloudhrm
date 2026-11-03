#include "Biometric.h"

#include "../config.h"
#include "../core/Logger.h"
#include "../ui/Ui.h"

void Biometric::begin(Ui& ui) {
  ui_ = &ui;
#if CHRM_SERIAL_DEBUG
  LOGW("Serial debug build: fingerprint sensor disabled (it shares UART0)");
  present_ = false;
#else
  finger_.begin(cfg::FINGERPRINT_BAUD);
  present_ = finger_.verifyPassword();
  if (present_) {
    if (finger_.getParameters() == FINGERPRINT_OK) capacity_ = finger_.capacity;
    LOGI("Fingerprint sensor OK, capacity %u", capacity_);
  } else {
    LOGE("Fingerprint sensor not found");
  }
#endif
}

Biometric::Scan Biometric::identify(uint16_t& id) {
  if (!present_) return Scan::None;
  uint32_t now = millis();
  if (now - lastPoll_ < 150) return Scan::None;
  lastPoll_ = now;

  uint8_t p = finger_.getImage();
  if (p == FINGERPRINT_NOFINGER) return Scan::None;
  if (p != FINGERPRINT_OK) return Scan::Error;

  p = finger_.image2Tz();
  if (p == FINGERPRINT_IMAGEMESS) return Scan::NoMatch;  // v1 showed "invalid" for this
  if (p != FINGERPRINT_OK) return Scan::Error;

  p = finger_.fingerFastSearch();
  if (p == FINGERPRINT_NOTFOUND) return Scan::NoMatch;
  if (p != FINGERPRINT_OK) return Scan::Error;

  id = finger_.fingerID;
  return Scan::Match;
}

uint16_t Biometric::templateCount() {
  if (!present_) return 0;
  if (finger_.getTemplateCount() != FINGERPRINT_OK) return 0;
  return finger_.templateCount;
}

// ---- enroll -----------------------------------------------------------------

void Biometric::startEnroll(uint16_t slot) {
  slot_ = slot;
  error_ = "";
  result_ = EnrollStatus::Running;
  if (!present_) {
    fail("Sensor missing");
    return;
  }
  LOGI("Enroll start, slot %u", slot);
  nextStep(Step::WaitFinger1);
}

void Biometric::cancelEnroll() {
  if (step_ != Step::Idle && step_ != Step::Done) fail("Cancelled");
}

void Biometric::nextStep(Step s) {
  step_ = s;
  deadline_ = millis() + cfg::ENROLL_STEP_TIMEOUT_MS;
  if (s == Step::WaitFinger1) ui_->fingerScan("Place finger");
  if (s == Step::WaitRemove) ui_->removeFinger();
  if (s == Step::WaitFinger2) ui_->fingerScan("Place again");
}

void Biometric::fail(const char* why) {
  error_ = why;
  result_ = EnrollStatus::Failed;
  step_ = Step::Done;
  LOGW("Enroll failed: %s", why);
}

Biometric::EnrollStatus Biometric::pollEnroll() {
  if (step_ == Step::Done || step_ == Step::Idle) return result_;
  if ((int32_t)(millis() - deadline_) >= 0) {
    fail("Timeout");
    return result_;
  }
  uint32_t now = millis();
  if (now - lastPoll_ < 100) return result_;
  lastPoll_ = now;

  uint8_t p = finger_.getImage();
  switch (step_) {
    case Step::WaitFinger1:
    case Step::WaitFinger2: {
      if (p == FINGERPRINT_NOFINGER) break;
      if (p != FINGERPRINT_OK) {
        ui_->fingerInvalid(0);  // v1 showed this on read errors and kept waiting
        break;
      }
      uint8_t buf = step_ == Step::WaitFinger1 ? 1 : 2;
      p = finger_.image2Tz(buf);
      if (p != FINGERPRINT_OK) {
        ui_->fingerInvalid(0);
        fail(p == FINGERPRINT_IMAGEMESS ? "Image too messy" : "Bad image");
        break;
      }
      ui_->fingerValid();
      if (step_ == Step::WaitFinger1) {
        nextStep(Step::WaitRemove);
        break;
      }
      p = finger_.createModel();
      if (p != FINGERPRINT_OK) {
        fail(p == FINGERPRINT_ENROLLMISMATCH ? "Fingers differ" : "Model failed");
        break;
      }
      p = finger_.storeModel(slot_);
      if (p != FINGERPRINT_OK) {
        fail(p == FINGERPRINT_BADLOCATION ? "Bad slot" : "Store failed");
        break;
      }
      LOGI("Enroll stored in slot %u", slot_);
      ui_->stored(0);
      result_ = EnrollStatus::Success;
      step_ = Step::Done;
      break;
    }
    case Step::WaitRemove:
      if (p == FINGERPRINT_NOFINGER) nextStep(Step::WaitFinger2);
      break;
    default:
      break;
  }
  return result_;
}
