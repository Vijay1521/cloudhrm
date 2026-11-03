# CloudHRM Terminal — Firmware v2

Firmware for the ESP8266 fingerprint + RFID attendance terminal. It is a rewrite of
`../CloudHRM.ino` (v1, left untouched) with the same features and **zero backend changes**:
every URL, form field, field order, MQTT setting and RFID ID format is identical to v1.

Design notes: `../docs/CloudHRM_Firmware_v2_Architecture.pdf`.

## Folder layout

```
CloudHRM_v2/
├── CloudHRM_v2.ino          entry point (setup/loop only)
├── src/
│   ├── config.h             URLs (verbatim from v1), MQTT, pins, timeouts, version
│   ├── app/                 App (main loop, punches, MQTT jobs), JobQueue
│   ├── cloud/               CloudApi (HTTPS), MqttLink, FormEncoder, trust_anchors.h
│   ├── core/                Settings, BootCounter, TimeSvc, Logger, Ota
│   ├── devices/             Biometric (fingerprint), RfidReader (RC522), Relay
│   ├── net/                 NetManager (WiFi range tuning, SmartConfig, hotspot)
│   ├── ui/                  Ui (OLED screens), bitmaps.h (v1 images)
│   └── web/                 Portal (web server + JSON API), portal_html.h (generated)
├── web/index.html           source of the setup web page
├── tools/build_web.py       packs web/index.html into src/web/portal_html.h
├── tools/make_trust_anchors.ps1  refreshes root CAs for the optional TLS verify mode
├── .vscode/                 VS Code tasks and IntelliSense
├── sketch.yaml              arduino-cli profile (board options + library versions)
└── platformio.ini           optional PlatformIO build
```

Each module has one job and a short `begin()` / `loop()`; nothing blocks for long, so
the portal, MQTT and the watchdog keep running during enrollment and reconnects.

## Building

### Arduino IDE 2

Open `CloudHRM_v2/CloudHRM_v2.ino`. Tools menu:

| Setting | Value |
|---|---|
| Board | NodeMCU 1.0 (ESP-12E Module) |
| CPU Frequency | 160 MHz |
| Flash Size | **4MB (FS:1MB OTA:~1019KB)** — needed for settings storage and OTA |
| SSL Support | All SSL ciphers (most compatible) |
| lwIP Variant | v2 Higher Bandwidth |
| Erase Flash | *All Flash Contents* for the first flash of a v1 unit, then *Only Sketch* |

Libraries (Library Manager), versions this was written against: ESP8266 core 3.1.2,
ArduinoJson 7.4.3, Adafruit SSD1306 2.5.17, Adafruit GFX 1.12.6, Adafruit BusIO 1.17.4,
Adafruit Fingerprint Sensor Library 2.1.4, MFRC522 1.4.12, PubSubClient 2.8.
NTPClient is no longer used.

The fingerprint sensor shares the USB serial pins: unplug the sensor's TX wire while
flashing over USB, or use OTA.

### VS Code

Install [arduino-cli](https://arduino.github.io/arduino-cli/) and the C/C++ extension,
open the `CloudHRM_v2` folder, then *Terminal → Run Task*:
**Build firmware**, **Upload (USB)**, **Upload (OTA over WiFi)**, **Serial monitor**.
`sketch.yaml` pins the same board options as the table above.

### After editing the web page

```
python tools/build_web.py
```

## First-time setup of a terminal

1. Power on. With no WiFi saved, the OLED alternates between **EPMode** (SmartConfig,
   120 s) and **APMode** (hotspot, 180 s).
2. Either:
   - **EspTouch app**: enter the WiFi password as `wifipassword$DEVICEID`, as with v1; or
   - **Hotspot**: join `CloudHRM-XXXXXX` with the password shown on the OLED. The setup page
     opens by itself (or browse to http://192.168.4.1). Set the admin password, then in
     the **WiFi** tab fill in **SSID**, **Password** and **Device ID** and press *Test & save*.
3. The terminal checks the credentials before saving them and shows the real reason if
   they fail (wrong password, network not found, ...).

After setup, the same page is available on the office network at the terminal's IP
(admin login required; can be switched off in *Device → Access*).

## Hotspot and master password

`src/secrets.h` (not committed; copy `src/secrets.example.h`) sets two fixed passwords:

- `CHRM_AP_PASSWORD`: WPA2 password of the setup hotspot `CloudHRM-XXXXXX`, the same on every terminal.
  It is not shown on the OLED.
- `CHRM_MASTER_PASSWORD`: always accepted at the portal sign-in, and as the *current password*
  when setting a new admin password (to recover a forgotten one). ArduinoOTA uses it until an
  admin password is set.

The release workflow writes `src/secrets.h` from the repository secret **`CHRM_MASTER_PASSWORD`**
(used for both). Anyone who has a firmware `.bin` can extract these values, so treat them as
"installer" passwords rather than strong secrets.

## Reset gestures (quick presses of the reset button, or power cycles, < 3 s apart)

| Presses | Action |
|---|---|
| 2 | Setup mode (SmartConfig / hotspot). Returns to the saved WiFi after 5 idle minutes |
| 4 | Forget WiFi (device ID, timezone and admin password kept) |
| 6 | Factory reset (everything, including the admin password) |

Crash or watchdog resets are not counted.

## Weak-signal behaviour

- Transmit power 20.5 dBm (maximum) and 802.11b first (best receiver sensitivity), falling
  back to g, then n, if the router refuses b. Modem sleep is off.
- Joins the strongest access point with the configured SSID; roams if the signal stays
  below −80 dBm and a clearly better AP exists.
- Reconnects with backoff (2 s … 60 s) without freezing; punches made while offline show
  *Network problem* (the API has no timestamp field, so punches cannot be queued).
- Optional static IP (WiFi tab → Advanced) avoids DHCP timeouts.
- The Status tab shows RSSI, a 10-minute signal graph and a site survey to choose a spot.

## Firmware updates (OTA)

All updates are **signed**. `public.key` (in this folder, committed) is built into the firmware,
and the terminal rejects any image not signed with the matching `private.key`.
Keep `private.key` secret and backed up offline (it is in `.gitignore`). If it is lost,
terminals can only be updated again over USB with a new key pair.
The ESP8266 core signs at build time by calling `openssl`, so `openssl` must be on PATH
(for example `C:\Program Files\OpenSSL-Win64\bin`). Without it, a build silently produces
only an unsigned `.bin`, which signed terminals refuse.

| Method | How |
|---|---|
| **GitHub (recommended)** | Tools tab → *Check for updates* → *Install*, or automatically at night (02:00–04:00, only when idle; switch in *Device → Access*) |
| **Web page upload** | Tools → *Firmware update from file*: upload `CloudHRM_v2.bin` from a GitHub release, or `CloudHRM_v2.ino.bin.signed` from your own build |
| **ArduinoOTA / VS Code** | VS Code task *Upload (OTA over WiFi)* sends the `.bin.signed` file. The Arduino IDE's network port uploads the **unsigned** `.bin`, so signed terminals reject it; use the portal or the VS Code task instead |

The Status tab shows flash size, firmware size and free OTA space.

### Setting up GitHub releases (one time)

1. Put this folder (`CloudHRM_v2`) in a GitHub repository as its root. The workflow is at
   `.github/workflows/firmware-release.yml`.
2. The repository must be **public** so terminals can download releases without a token (a
   token on every device could be read out of any one of them). To keep the source private,
   use a separate public repository just for releases.
3. In the repository, add the secret **`OTA_PRIVATE_KEY`** (*Settings → Secrets and variables →
   Actions*) with the full contents of `private.key`. Commit `public.key`; never commit `private.key`.
4. Set `GITHUB_OTA_REPO` in `src/config.h` to `"owner/repo"`, then flash that build once over USB.

### Releasing a new version

1. Bump `CHRM_FW_VERSION` in `src/config.h` (for example `"2.0.1"`) and commit.
2. `git tag v2.0.1` and `git push origin v2.0.1`.
3. GitHub Actions builds with the pinned core and libraries, signs, and publishes
   `CloudHRM_v2.bin` and `manifest.json` to the release. The workflow fails if the tag does not
   match the version or the secret does not match `public.key`.
4. Terminals pick it up that night, or straight away via *Check for updates*.

Terminals connect only to github.com and GitHub's file servers for this, verifying their
certificates against `src/cloud/trust_anchors.h`; the CloudHRM server is not involved. The
clock must be synced (NTP) before a check can run.

## TLS

`CHRM_TLS_MODE` in `src/config.h`:

- `CHRM_TLS_INSECURE` (default): no certificate check, exactly like v1.
- `CHRM_TLS_VERIFY`: verifies login.cloudhrm.in against the root CAs in
  `src/cloud/trust_anchors.h` (Let's Encrypt chain used today plus other major roots).
  Requires working NTP; while the time is not synced, punches show *Network problem*.
  Refresh the roots with `tools/make_trust_anchors.ps1`.

## Behaviour changes from v1 (device side only)

- Network failures show *Network problem* instead of *ID NOT FOUND*.
- The same card or finger is ignored for 5 s (no double punches).
- RFID enrollment waits 15 s for a card (v1: about 3 s); fingerprint enrollment steps time out after 20 s.
- The setup hotspot is WPA2-protected and the portal needs an admin password.
- WiFi passwords up to 63 characters (v1: 20); device IDs up to 64 characters.
- Default timezone is IST (+05:30) until the server sends `RTC_update`; `+05:45` style offsets now parse correctly.
- Logs are in the portal (Tools → Show log) instead of the serial port, which belongs to the fingerprint sensor.
  For bench debugging build with `CHRM_SERIAL_DEBUG=1` (fingerprint sensor disabled in that build).
