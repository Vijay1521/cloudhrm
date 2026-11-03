// Small FIFO of remote commands. The MQTT callback only pushes here; the
// app runs the jobs from loop(), so the callback never blocks.
#pragma once

#include <Arduino.h>

enum class JobType : uint8_t { EnrollFinger, EnrollCard, SetTimezone };

struct Job {
  JobType type = JobType::SetTimezone;
  String employeeId;
  String deviceType;
  String value;  // RTC_update text for SetTimezone
};

class JobQueue {
 public:
  static constexpr uint8_t kCapacity = 4;

  bool push(const Job& j) {
    if (count_ == kCapacity) return false;
    items_[(head_ + count_) % kCapacity] = j;
    count_++;
    return true;
  }
  bool pop(Job& out) {
    if (count_ == 0) return false;
    out = items_[head_];
    items_[head_] = Job();
    head_ = (head_ + 1) % kCapacity;
    count_--;
    return true;
  }
  uint8_t size() const { return count_; }

 private:
  Job items_[kCapacity];
  uint8_t head_ = 0;
  uint8_t count_ = 0;
};
