// R307 / AS608 fingerprint sensor on UART0: identify (attendance) and a
// non-blocking enroll state machine with a timeout on every step.
#pragma once

#include <Adafruit_Fingerprint.h>
#include <Arduino.h>

class Ui;

class Biometric {
 public:
  enum class Scan { None, Match, NoMatch, Error };
  enum class EnrollStatus { Running, Success, Failed };

  void begin(Ui& ui);
  bool present() const { return present_; }

  // One identify attempt, throttled. On Match, id holds the sensor slot.
  Scan identify(uint16_t& id);

  void startEnroll(uint16_t slot);
  EnrollStatus pollEnroll();
  void cancelEnroll();
  const char* enrollError() const { return error_; }

  uint16_t templateCount();
  uint16_t capacity() const { return capacity_; }

 private:
  enum class Step { Idle, WaitFinger1, WaitRemove, WaitFinger2, Done };

  void fail(const char* why);
  void nextStep(Step s);

  Adafruit_Fingerprint finger_{&Serial};
  Ui* ui_ = nullptr;
  bool present_ = false;
  uint16_t capacity_ = 0;
  uint32_t lastPoll_ = 0;

  Step step_ = Step::Idle;
  EnrollStatus result_ = EnrollStatus::Running;
  uint16_t slot_ = 0;
  uint32_t deadline_ = 0;
  const char* error_ = "";
};
