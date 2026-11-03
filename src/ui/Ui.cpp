#include "Ui.h"

#include "../config.h"
#include "../core/Logger.h"
#include "bitmaps.h"

bool Ui::begin() {
  ok_ = d_.begin(SSD1306_SWITCHCAPVCC, cfg::OLED_I2C_ADDR);
  if (!ok_) LOGE("OLED not found at 0x%02X", cfg::OLED_I2C_ADDR);
  d_.setTextColor(WHITE);
  d_.clearDisplay();
  d_.display();
  return ok_;
}

bool Ui::isHeld() const { return holdUntil_ != 0 && (int32_t)(millis() - holdUntil_) < 0; }

void Ui::start() {
  d_.clearDisplay();
  d_.setTextColor(WHITE);
  d_.setTextSize(1);
  d_.setTextWrap(true);
}

void Ui::finish(uint32_t holdMs) {
  if (ok_) d_.display();
  holdUntil_ = holdMs ? (millis() + holdMs) | 1 : 0;
}

void Ui::drawSignal(int x, int y, int bars) {
  for (int b = 0; b < 4; b++) {
    int h = 2 + b * 2;
    if (b < bars)
      d_.fillRect(x + b * 4, y + 8 - h, 3, h, WHITE);
    else
      d_.drawPixel(x + b * 4 + 1, y + 7, WHITE);
  }
}

void Ui::countdown(uint32_t secs, uint16_t color) {
  d_.setTextSize(1);
  d_.setTextColor(color);
  d_.setCursor(104, 56);
  d_.printf("%3lus", (unsigned long)secs);
  d_.setTextColor(WHITE);
}

int Ui::barsForRssi(int32_t rssi) {
  if (rssi == 0 || rssi < -88) return 0;
  if (rssi >= -60) return 4;
  if (rssi >= -70) return 3;
  if (rssi >= -80) return 2;
  return 1;
}

// ---- idle / status ----------------------------------------------------------

void Ui::pleaseWait(const char* deviceId) {  // v1 Wifi_Not_Connect()
  start();
  d_.setTextSize(2);
  d_.setCursor(0, 10);
  d_.println("Please");
  d_.setCursor(10, 35);
  d_.println("Wait");
  d_.setTextSize(1);
  d_.setCursor(10, 55);
  d_.print(deviceId);
  d_.drawBitmap(73, 10, Wifi_start_bits, Wifi_start_width, Wifi_start_height, WHITE);
  finish(0);
}

void Ui::smartConfig(const char* deviceId, uint32_t secsLeft) {  // v1 EPMODE()
  start();
  d_.setTextSize(2);
  d_.setCursor(0, 8);
  d_.println("EPMode");
  d_.setCursor(0, 30);
  d_.println("Connect");
  d_.setTextSize(1);
  d_.setCursor(0, 56);
  d_.print(deviceId[0] ? deviceId : "EspTouch app");
  d_.drawBitmap(73, 6, Wifi_start_bits, Wifi_start_width, Wifi_start_height, WHITE);
  countdown(secsLeft, WHITE);
  finish(0);
}

void Ui::apMode(const String& ssid, const char* pass, uint32_t secsLeft) {  // v1 APMODE()
  start();
  d_.setTextSize(2);
  d_.setCursor(0, 0);
  d_.println("APMode");
  d_.setTextSize(1);
  d_.setCursor(0, 22);
  d_.print("WiFi: ");
  d_.println(ssid);
  d_.setCursor(0, 33);
  d_.print("Pass: ");
  d_.println(pass);
  d_.setCursor(0, 44);
  d_.println("Open 192.168.4.1");
  countdown(secsLeft, WHITE);
  finish(0);
}

void Ui::connecting(const char* ssid) {
  start();
  d_.setTextSize(2);
  d_.setCursor(0, 8);
  d_.println("Joining");
  d_.setTextSize(1);
  d_.setCursor(0, 34);
  d_.println(ssid);
  d_.drawBitmap(73, 10, Wifi_start_bits, Wifi_start_width, Wifi_start_height, WHITE);
  finish(0);
}

void Ui::networkProblem(const char* detail, uint32_t holdMs) {  // v1 Network_problem()
  start();
  d_.setTextSize(2);
  d_.setCursor(42, 8);
  d_.println("Network");
  d_.setCursor(42, 32);
  d_.println("Problem");
  d_.drawBitmap(-5, 0, LTE_networkIMG, LOGO_WIDTH_LTE, LOGO_HEIGHT, WHITE);
  if (detail && detail[0]) {
    d_.setTextSize(1);
    d_.setCursor(42, 55);
    d_.print(detail);
  }
  finish(holdMs);
}

void Ui::needDeviceId(const String& ip) {
  start();
  d_.setTextSize(1);
  d_.setCursor(0, 4);
  d_.println("Device ID not set.");
  d_.println();
  d_.println("Open in a browser:");
  d_.setTextSize(1);
  d_.print("http://");
  d_.println(ip);
  finish(0);
}

void Ui::clock(const String& date, const String& time, int bars, bool synced) {  // v1 Nettime()
  start();
  drawSignal(111, 0, bars);
  d_.setTextSize(2);
  if (synced) {
    d_.setCursor(7, 10);
    d_.print(date);
    d_.setCursor(7, 35);
    d_.print(time);
  } else {
    d_.setCursor(7, 18);
    d_.print("Syncing");
    d_.setCursor(7, 40);
    d_.print("time...");
  }
  finish(0);
}

// ---- events -----------------------------------------------------------------

void Ui::cardScanning(uint32_t holdMs) {
  start();
  d_.drawBitmap(0, 0, RFid_readimg, LOGO_WIDTH, LOGO_HEIGHT, WHITE);
  finish(holdMs);
}

void Ui::tapCard(uint32_t secsLeft) {  // v1 Welcome()
  start();
  d_.drawBitmap(0, 0, epd_bitmap_RFID, LOGO_WIDTH, LOGO_HEIGHT, WHITE);
  countdown(secsLeft, BLACK);
  finish(0);
}

void Ui::fingerValid(uint32_t holdMs) {
  start();
  d_.drawBitmap(34, 0, FinPr_valid_bits, FinPr_valid_width, FinPr_valid_height, WHITE);
  finish(holdMs);
}

void Ui::fingerInvalid(uint32_t holdMs) {
  start();
  d_.drawBitmap(34, 0, FinPr_invalid_bits, FinPr_invalid_width, FinPr_invalid_height, WHITE);
  finish(holdMs);
}

void Ui::fingerFailed(uint32_t holdMs) {
  start();
  d_.drawBitmap(32, 0, FinPr_failed_bits, FinPr_failed_width, FinPr_failed_height, WHITE);
  finish(holdMs);
}

void Ui::fingerScan(const char* caption) {
  start();
  d_.drawBitmap(34, 0, FinPr_scan_bits, FinPr_scan_width, FinPr_scan_height, WHITE);
  d_.setTextSize(1);
  d_.setCursor(0, 0);
  d_.print(caption);
  finish(0);
}

void Ui::removeFinger() {
  text2("Remove", "finger", 0);
}

void Ui::stored(uint32_t holdMs) {
  start();
  d_.setTextSize(2);
  d_.setCursor(25, 25);
  d_.print("Stored");
  finish(holdMs);
}

void Ui::employeeName(const String& fullName, uint32_t holdMs) {  // v1 firstname() + lastname()
  String name = fullName;
  name.trim();
  int sp = name.indexOf(' ');
  String first = sp < 0 ? name : name.substring(0, sp);
  String second;
  if (sp >= 0) {
    String rest = name.substring(sp + 1);
    rest.trim();
    int sp2 = rest.indexOf(' ');
    second = sp2 < 0 ? rest : rest.substring(0, sp2);
  }
  start();
  d_.setTextSize(2);
  d_.setTextWrap(false);
  d_.setCursor(0, 15);
  d_.print(first);
  d_.setCursor(0, 40);
  d_.print(second);
  finish(holdMs);
}

void Ui::message(const String& text, uint32_t holdMs) {
  start();
  d_.setCursor(0, 15);
  d_.print(text);
  finish(holdMs);
}

void Ui::idNotFound(uint32_t holdMs) {  // v1 Attendance_Not_Register()
  start();
  d_.setTextSize(2);
  d_.setCursor(23, 10);
  d_.println("ID NOT");
  d_.setCursor(28, 40);
  d_.print("FOUND");
  finish(holdMs);
}

void Ui::timezoneUpdated(uint32_t holdMs) {
  start();
  d_.drawBitmap(0, 0, Clockbitmapclock, LOGO_WIDTH, LOGO_HEIGHT, WHITE);
  finish(holdMs);
}

void Ui::text2(const char* line1, const char* line2, uint32_t holdMs) {
  start();
  d_.setTextSize(2);
  d_.setCursor(0, 8);
  d_.print(line1);
  d_.setCursor(0, 32);
  d_.print(line2);
  finish(holdMs);
}

void Ui::progress(const char* title, int percent) {
  start();
  d_.setTextSize(2);
  d_.setCursor(0, 8);
  d_.print(title);
  d_.drawRect(0, 40, 128, 12, WHITE);
  d_.fillRect(2, 42, (124 * constrain(percent, 0, 100)) / 100, 8, WHITE);
  finish(0);
}
