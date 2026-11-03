// CloudHRM biometric + RFID attendance terminal, firmware v2.
// Board: NodeMCU 1.0 (ESP-12E), Flash size "4MB (FS:1MB OTA:~1019KB)".
// See README.md for build settings, wiring and the module layout.
//
// All logic lives under src/; this file only starts the application.

#include "src/app/App.h"

App app;

void setup() { app.begin(); }

void loop() { app.loop(); }
