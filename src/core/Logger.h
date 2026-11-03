// In-RAM ring buffer of log lines, shown in the web portal (/api/logs).
// UART0 belongs to the fingerprint sensor, so logs only reach Serial in
// CHRM_SERIAL_DEBUG builds. Never log passwords.
#pragma once

#include <Arduino.h>

namespace Log {

void begin();
void writeP(char level, PGM_P fmt, ...) __attribute__((format(printf, 2, 3)));
// All buffered lines, oldest first.
String dump();

}  // namespace Log

#define LOGI(fmt, ...) Log::writeP('I', PSTR(fmt), ##__VA_ARGS__)
#define LOGW(fmt, ...) Log::writeP('W', PSTR(fmt), ##__VA_ARGS__)
#define LOGE(fmt, ...) Log::writeP('E', PSTR(fmt), ##__VA_ARGS__)
