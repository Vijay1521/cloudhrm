// Template for src/secrets.h, which is NOT committed (see .gitignore).
// Copy this file to src/secrets.h and set the real values. The GitHub release
// workflow writes src/secrets.h from the repository secret CHRM_MASTER_PASSWORD.
#pragma once

// WPA2 password of the setup hotspot (CloudHRM-XXXXXX), 8-63 characters.
#define CHRM_AP_PASSWORD "change-me"

// Master password: always accepted by the web portal (and as the current
// password when setting a new admin password), and by ArduinoOTA until an
// admin password is set. 8-64 characters.
#define CHRM_MASTER_PASSWORD "change-me"
