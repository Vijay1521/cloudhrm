#include "Logger.h"

#include <stdarg.h>

#include "../config.h"

namespace Log {
namespace {

constexpr size_t kBufSize = 4096;
char buf[kBufSize];
size_t head = 0;   // next write position
bool wrapped = false;

void put(const char* s, size_t n) {
  for (size_t i = 0; i < n; i++) {
    buf[head++] = s[i];
    if (head == kBufSize) {
      head = 0;
      wrapped = true;
    }
  }
}

}  // namespace

void begin() {
#if CHRM_SERIAL_DEBUG
  Serial.begin(115200);
  Serial.println();
#endif
}

void writeP(char level, PGM_P fmt, ...) {
  char line[176];
  uint32_t s = millis() / 1000;
  int n = snprintf_P(line, sizeof(line), PSTR("%5lu.%c "), (unsigned long)s, level);
  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf_P(line + n, sizeof(line) - n - 1, fmt, ap);
  va_end(ap);
  if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
  line[n++] = '\n';
  line[n] = 0;
  put(line, n);
#if CHRM_SERIAL_DEBUG
  Serial.print(line);
#endif
}

String dump() {
  String out;
  if (wrapped) {
    out.reserve(kBufSize);
    // Skip the partial first line that the ring buffer cut in half.
    size_t start = head;
    while (start < kBufSize && buf[start] != '\n') start++;
    for (size_t i = start + 1; i < kBufSize; i++) out += buf[i];
  } else {
    out.reserve(head);
  }
  for (size_t i = 0; i < head; i++) out += buf[i];
  return out;
}

}  // namespace Log
