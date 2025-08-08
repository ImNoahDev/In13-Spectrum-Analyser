#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <MSGEQ7.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ===========================
// Hardware configuration
// ===========================
// Adjust these pin assignments to match your wiring
// MSGEQ7 connections (shared reset & strobe, one analog out)
static constexpr uint8_t PIN_MSGEQ7_RESET = 26;   // any digital pin
static constexpr uint8_t PIN_MSGEQ7_STROBE = 25;  // any digital pin
static constexpr uint8_t PIN_MSGEQ7_ANALOG = 34;  // IO34

// Tube output PWM pins (7 tubes)
static constexpr uint8_t TUBE_PINS[7] = {13, 12, 14, 27, 33, 32, 15};

// PSU enable control pin (active HIGH assumed)
static constexpr uint8_t PIN_PSU_ENABLE = 23;

// Power button (active LOW with pull-up). Use external pull-up if using input-only pins.
static constexpr uint8_t PIN_POWER_BUTTON = 22;

// Potentiometers (10k, reversed so high resistance yields 0)
static constexpr uint8_t PIN_POT_GAIN = 35;  // IO35
static constexpr uint8_t PIN_POT_UNUSED = 16; // IO16 (optional spare)

// ===========================
// PWM (LEDC) configuration
// ===========================
static constexpr uint8_t LEDC_CHANNEL_TUBES[7] = {0, 1, 2, 3, 4, 5, 6};
static constexpr uint32_t LEDC_FREQ_TUBE = 20000;      // 20 KHz for height control
static constexpr uint8_t LEDC_RES_BITS = 12;           // 0..4095
static constexpr uint16_t LEDC_MAX = (1 << LEDC_RES_BITS) - 1; // 4095

// Dual-core sync
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool g_newBandsReady = false;
TaskHandle_t g_audioTaskHandle = nullptr;

// ===========================
// MSGEQ7 library object (NicoHood/MSGEQ7)
// ===========================
// Smoothing: 191 (~75%) recommended if smoothing desired
CMSGEQ7<191, PIN_MSGEQ7_RESET, PIN_MSGEQ7_STROBE, PIN_MSGEQ7_ANALOG> MSGEQ7;

// Reads per second limit (configurable 1..7 from panel, default 5)
static uint8_t g_updatesPerSecond = 5; // 1..7 limited by settings mode
#define MSGEQ7_INTERVAL ReadsPerSecond(g_updatesPerSecond)

// ===========================
// Runtime state
// ===========================
bool g_isOn = false;                // device power state
bool g_inCalibration = false;       // calibration mode active
uint8_t g_calibIndex = 0;           // which tube we are calibrating 0..6
bool g_inSettings = false;          // settings mode active
bool g_mirrorMode = false;          // experimental mirror mode

// Track last audio time for auto-off
uint32_t g_lastLoudMs = 0;
static constexpr uint32_t AUTO_OFF_MS = 5UL * 60UL * 1000UL; // 5 minutes

// Auto-on threshold on detected audio
static uint8_t g_audioThreshold = 25; // noise floor ~20 in 8-bit; adjust via web

// Volume gain (from pot). We'll map pot to 0.25..2.0
float g_volumeGain = 1.0f;



// Calibration maxima per tube (0..4095 for PWM duty)
uint16_t g_tubeMaxDuty[7] = {4095,4095,4095,4095,4095,4095,4095};

// For power-toggle rapid reset (10 toggles in 2 minutes)
static constexpr uint8_t TOGGLE_RESET_COUNT = 10;
static constexpr uint32_t TOGGLE_WINDOW_MS = 2UL * 60UL * 1000UL;
uint32_t g_toggleTimestamps[TOGGLE_RESET_COUNT] = {0};
uint8_t g_toggleIdx = 0;

// ===========================
// WiFi / Web
// ===========================
WebServer server(80);
WiFiUDP udp;
static constexpr uint16_t UDP_PORT = 4210;

String g_deviceName;
String g_lastWifiSsid;
String g_lastWifiPass;

// Pairing (store up to 5 peers)
static constexpr uint8_t MAX_PEERS = 5;
IPAddress g_peers[MAX_PEERS];
uint8_t g_peerCount = 0;

// ===========================
// EEPROM layout (ESP32 emulated)
// ===========================
// We'll allocate 1024 bytes and manage offsets manually
static constexpr size_t EEPROM_SIZE = 1024;
// Layout:
// 0x00: signature 'S','A','1'
// 0x03: flags (bit0: mirrorMode)
// 0x04..0x13: tubeMaxDuty[7] as uint16_t (14 bytes)
// 0x18: updatesPerSecond (1 byte)
// 0x19: audioThreshold (1 byte)
// 0x1A..0x2F: peers (5 * 4 bytes)
// 0x30: peerCount (1 byte)
// 0x40..0x9F: wifi ssid (96 bytes)
// 0xA0..0xFF: wifi pass (96 bytes)

static constexpr int EE_OFFS_SIGNATURE = 0x00;
static constexpr int EE_OFFS_FLAGS = 0x03;
static constexpr int EE_OFFS_TUBE_MAX = 0x04; // 14 bytes
static constexpr int EE_OFFS_UPS = 0x18;
static constexpr int EE_OFFS_THRESH = 0x19;
static constexpr int EE_OFFS_PEERS = 0x1A; // 20 bytes
static constexpr int EE_OFFS_PEERCOUNT = 0x30;
static constexpr int EE_OFFS_WIFI_SSID = 0x40; // len 96
static constexpr int EE_OFFS_WIFI_PASS = 0xA0; // len 96

// ===========================
// Utilities
// ===========================
static inline uint32_t nowMs() { return millis(); }

static void eepromWriteBytes(int addr, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) EEPROM.write(addr + i, data[i]);
}
static void eepromReadBytes(int addr, uint8_t* out, size_t len) {
  for (size_t i = 0; i < len; ++i) out[i] = EEPROM.read(addr + i);
}

static void eepromWriteString(int addr, const String& s, size_t maxLen) {
  size_t n = min(s.length(), maxLen - 1);
  for (size_t i = 0; i < n; ++i) EEPROM.write(addr + i, (uint8_t)s[i]);
  EEPROM.write(addr + n, 0);
  for (size_t i = n + 1; i < maxLen; ++i) EEPROM.write(addr + i, 0);
}
static String eepromReadString(int addr, size_t maxLen) {
  String s;
  for (size_t i = 0; i < maxLen; ++i) {
    uint8_t c = EEPROM.read(addr + i);
    if (c == 0) break;
    s += (char)c;
  }
  return s;
}

static void saveSettingsToEEPROM() {
  EEPROM.write(EE_OFFS_SIGNATURE + 0, 'S');
  EEPROM.write(EE_OFFS_SIGNATURE + 1, 'A');
  EEPROM.write(EE_OFFS_SIGNATURE + 2, '1');
  uint8_t flags = 0;
  if (g_mirrorMode) flags |= 0x01;
  EEPROM.write(EE_OFFS_FLAGS, flags);
  for (uint8_t i = 0; i < 7; ++i) {
    uint16_t v = g_tubeMaxDuty[i];
    EEPROM.write(EE_OFFS_TUBE_MAX + i * 2 + 0, (uint8_t)(v & 0xFF));
    EEPROM.write(EE_OFFS_TUBE_MAX + i * 2 + 1, (uint8_t)(v >> 8));
  }
  EEPROM.write(EE_OFFS_UPS, g_updatesPerSecond);
  EEPROM.write(EE_OFFS_THRESH, g_audioThreshold);
  // peers
  for (uint8_t i = 0; i < MAX_PEERS; ++i) {
    uint32_t ip = (i < g_peerCount) ? (uint32_t)g_peers[i] : 0;
    EEPROM.write(EE_OFFS_PEERS + i * 4 + 0, (uint8_t)(ip & 0xFF));
    EEPROM.write(EE_OFFS_PEERS + i * 4 + 1, (uint8_t)((ip >> 8) & 0xFF));
    EEPROM.write(EE_OFFS_PEERS + i * 4 + 2, (uint8_t)((ip >> 16) & 0xFF));
    EEPROM.write(EE_OFFS_PEERS + i * 4 + 3, (uint8_t)((ip >> 24) & 0xFF));
  }
  EEPROM.write(EE_OFFS_PEERCOUNT, g_peerCount);
  eepromWriteString(EE_OFFS_WIFI_SSID, g_lastWifiSsid, 96);
  eepromWriteString(EE_OFFS_WIFI_PASS, g_lastWifiPass, 96);
  EEPROM.commit();
}

static void loadSettingsFromEEPROM() {
  if (EEPROM.read(EE_OFFS_SIGNATURE + 0) == 'S' &&
      EEPROM.read(EE_OFFS_SIGNATURE + 1) == 'A' &&
      EEPROM.read(EE_OFFS_SIGNATURE + 2) == '1') {
    uint8_t flags = EEPROM.read(EE_OFFS_FLAGS);
    g_mirrorMode = (flags & 0x01) != 0;
    for (uint8_t i = 0; i < 7; ++i) {
      uint8_t l = EEPROM.read(EE_OFFS_TUBE_MAX + i * 2 + 0);
      uint8_t h = EEPROM.read(EE_OFFS_TUBE_MAX + i * 2 + 1);
      uint16_t v = (uint16_t)l | ((uint16_t)h << 8);
      if (v == 0 || v > LEDC_MAX) v = LEDC_MAX;
      g_tubeMaxDuty[i] = v;
    }
    uint8_t ups = EEPROM.read(EE_OFFS_UPS);
    if (ups >= 1 && ups <= 7) g_updatesPerSecond = ups; else g_updatesPerSecond = 5;
    uint8_t thr = EEPROM.read(EE_OFFS_THRESH);
    g_audioThreshold = thr == 0 ? 25 : thr;
    // peers
    g_peerCount = EEPROM.read(EE_OFFS_PEERCOUNT);
    if (g_peerCount > MAX_PEERS) g_peerCount = 0;
    for (uint8_t i = 0; i < g_peerCount; ++i) {
      uint32_t ip = 0;
      ip |= (uint32_t)EEPROM.read(EE_OFFS_PEERS + i * 4 + 0);
      ip |= (uint32_t)EEPROM.read(EE_OFFS_PEERS + i * 4 + 1) << 8;
      ip |= (uint32_t)EEPROM.read(EE_OFFS_PEERS + i * 4 + 2) << 16;
      ip |= (uint32_t)EEPROM.read(EE_OFFS_PEERS + i * 4 + 3) << 24;
      g_peers[i] = IPAddress(ip);
    }
    g_lastWifiSsid = eepromReadString(EE_OFFS_WIFI_SSID, 96);
    g_lastWifiPass = eepromReadString(EE_OFFS_WIFI_PASS, 96);
  }
}

static void eraseCalibration() {
  for (uint8_t i = 0; i < 7; ++i) g_tubeMaxDuty[i] = LEDC_MAX;
  saveSettingsToEEPROM();
}

static void eraseSettingsKeepCalibration() {
  bool prevMirror = g_mirrorMode;
  uint16_t prevMax[7];
  for (uint8_t i = 0; i < 7; ++i) prevMax[i] = g_tubeMaxDuty[i];
  // clear EEPROM
  for (int i = 0; i < (int)EEPROM_SIZE; ++i) EEPROM.write(i, 0);
  EEPROM.commit();
  // restore calib fields
  g_mirrorMode = prevMirror;
  for (uint8_t i = 0; i < 7; ++i) g_tubeMaxDuty[i] = prevMax[i];
  saveSettingsToEEPROM();
}

static void resetWifiCredentials() {
  g_lastWifiSsid = "";
  g_lastWifiPass = "";
  saveSettingsToEEPROM();
}

// ===========================
// LEDC helpers
// ===========================
static void ledcInitAll() {
  // tube channels
  for (uint8_t i = 0; i < 7; ++i) {
    ledcSetup(LEDC_CHANNEL_TUBES[i], LEDC_FREQ_TUBE, LEDC_RES_BITS);
    ledcAttachPin(TUBE_PINS[i], LEDC_CHANNEL_TUBES[i]);
    ledcWrite(LEDC_CHANNEL_TUBES[i], 0);
  }
}

static void setTubeDuty(uint8_t idx, uint16_t duty) {
  if (idx >= 7) return;
  duty = min<uint16_t>(duty, g_tubeMaxDuty[idx]);
  if (duty > LEDC_MAX) duty = LEDC_MAX;
  ledcWrite(LEDC_CHANNEL_TUBES[idx], duty);
}

static void clearAllTubes() {
  for (uint8_t i = 0; i < 7; ++i) ledcWrite(LEDC_CHANNEL_TUBES[i], 0);
}

// ===========================
// Button handling
// ===========================
static constexpr uint32_t DEBOUNCE_MS = 30;
static constexpr uint32_t CLICK_WINDOW_MS = 1000; // 1s triple-click window
static constexpr uint32_t LONG_PRESS_MS = 1200;

bool btnLastLevel = true; // pull-up, true=HIGH (released)
uint32_t btnLastChangeMs = 0;
uint32_t btnPressStartMs = 0;
uint8_t btnClickCount = 0;
uint32_t btnFirstClickMs = 0;

static void handleButton() {
  bool level = digitalRead(PIN_POWER_BUTTON); // HIGH: released, LOW: pressed (active low)
  uint32_t now = nowMs();
  if (level != btnLastLevel && (now - btnLastChangeMs) > DEBOUNCE_MS) {
    btnLastLevel = level;
    btnLastChangeMs = now;
    if (!level) { // pressed
      btnPressStartMs = now;
    } else { // released
      uint32_t pressDur = now - btnPressStartMs;
      // In calibration: any short release advances immediately
      if (g_inCalibration) {
        advanceCalibrationOnShortClick();
        return;
      }
      // In settings: any short release saves & exits immediately
      if (g_inSettings) {
        saveSettingsAndExit();
        return;
      }
      // Normal mode
      if (pressDur >= LONG_PRESS_MS) {
        // long press: open settings
        g_inSettings = true;
        g_inCalibration = false;
        g_isOn = true;
        digitalWrite(PIN_PSU_ENABLE, HIGH);
      } else {
        // short click for sequence (single/tiple)
        if (btnClickCount == 0) btnFirstClickMs = now;
        btnClickCount++;
      }
    }
  }
  // Evaluate click sequences (only when not in special modes)
  if (!g_inCalibration && !g_inSettings && btnClickCount > 0 && (now - btnFirstClickMs) > CLICK_WINDOW_MS) {
    uint8_t n = btnClickCount;
    btnClickCount = 0;
    if (n >= 3) {
      // triple click -> calibration mode
      g_inCalibration = true;
      g_inSettings = false;
      g_calibIndex = 0;
      g_isOn = true;
      digitalWrite(PIN_PSU_ENABLE, HIGH);
      clearAllTubes();
    } else if (n == 1) {
      // single click -> toggle power
      g_isOn = !g_isOn;
      digitalWrite(PIN_PSU_ENABLE, g_isOn ? HIGH : LOW);
      if (!g_isOn) clearAllTubes();
      // track toggles for WiFi reset logic
      g_toggleTimestamps[g_toggleIdx] = now;
      g_toggleIdx = (g_toggleIdx + 1) % TOGGLE_RESET_COUNT;
    }
  }
  // Check rapid toggles in window
  uint8_t count = 0;
  for (uint8_t i = 0; i < TOGGLE_RESET_COUNT; ++i) {
    if (now - g_toggleTimestamps[i] <= TOGGLE_WINDOW_MS && g_toggleTimestamps[i] != 0) count++;
  }
  if (count >= TOGGLE_RESET_COUNT) {
    // reset WiFi creds, start AP if necessary
    for (uint8_t i = 0; i < TOGGLE_RESET_COUNT; ++i) g_toggleTimestamps[i] = 0;
    resetWifiCredentials();
    if (WiFi.getMode() != WIFI_AP) {
      WiFi.disconnect(true, true);
      delay(200);
      WiFi.mode(WIFI_AP);
      String ssid = g_deviceName;
      ssid += "-Setup";
      WiFi.softAP(ssid.c_str());
    }
  }
}

// ===========================
// Audio reading & tube update
// ===========================
static uint8_t g_lastBands[7] = {0};

static void updatePots() {
  // Pot orientation reversed; scale and invert
  int rawV = analogRead(PIN_POT_GAIN);
  float v = 1.0f - (float)rawV / 4095.0f;
  // Smooth a bit (optional)
  static float sv = 1.0f;
  sv = sv * 0.85f + v * 0.15f;
  g_volumeGain = 0.25f + sv * 1.75f; // 0.25..2.0
}

static void applyBandsToTubes(const uint8_t bands[7]) {
  for (uint8_t i = 0; i < 7; ++i) {
    // Map 0..255 band value to 0..LEDC_MAX duty, apply per-tube calibration
    uint16_t duty = (uint16_t)((bands[i] / 255.0f) * LEDC_MAX);
    setTubeDuty(i, duty);
  }
}

static bool isAudioLoud(const uint8_t bands[7]) {
  // Compute a simple average
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 7; ++i) sum += bands[i];
  uint8_t avg = sum / 7;
  return avg >= g_audioThreshold;
}

static void readAudioAndUpdate() {
  bool newReading = MSGEQ7.read(MSGEQ7_INTERVAL);
  if (!newReading) return;

  uint8_t bands[7];
  for (uint8_t i = 0; i < 7; ++i) {
    uint8_t x = MSGEQ7.get(i);
    x = mapNoise(x);    // remove static noise floor
    float f = min(255.0f, x * g_volumeGain);
    bands[i] = (uint8_t)f;
  }

  // Auto power logic
  if (isAudioLoud(bands)) {
    g_lastLoudMs = nowMs();
    if (!g_isOn) {
      portENTER_CRITICAL(&g_mux);
      g_isOn = true;
      digitalWrite(PIN_PSU_ENABLE, HIGH);
      portEXIT_CRITICAL(&g_mux);
    }
  }
  if (g_isOn && (nowMs() - g_lastLoudMs) > AUTO_OFF_MS) {
    portENTER_CRITICAL(&g_mux);
    g_isOn = false;
    digitalWrite(PIN_PSU_ENABLE, LOW);
    portEXIT_CRITICAL(&g_mux);
    clearAllTubes();
  }

  // Apply locally only if not mirroring and not in UI modes
  if (g_isOn && !g_inCalibration && !g_inSettings && !g_mirrorMode) {
    applyBandsToTubes(bands);
  }

  // Publish new bands snapshot for loop()/UDP to use
  portENTER_CRITICAL(&g_mux);
  memcpy(g_lastBands, bands, 7);
  g_newBandsReady = true;
  portEXIT_CRITICAL(&g_mux);
}

// Audio task pinned to core 0
void audioTask(void* pv) {
  for (;;) {
    // Only analyze when not in calibration/settings; still keep timers running
    readAudioAndUpdate();
    // Small delay to yield; MSGEQ7.read() enforces rate via interval
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// ===========================
// Calibration & Settings UIs (panel side)
// ===========================
static void doCalibrationStep() {
  // Illuminate only current tube, others off
  for (uint8_t i = 0; i < 7; ++i) {
    if (i == g_calibIndex) {
      // Use gain pot to set max duty
      int rawV = analogRead(PIN_POT_GAIN);
      float v = 1.0f - (float)rawV / 4095.0f; // reversed
      uint16_t maxDuty = (uint16_t)(v * LEDC_MAX);
      if (maxDuty < 10) maxDuty = 10;
      g_tubeMaxDuty[i] = maxDuty;
      setTubeDuty(i, maxDuty);
    } else {
      setTubeDuty(i, 0);
    }
  }
  // Pressing the power button (short) advances to next tube
  // This is handled in handleButton() which sets g_inCalibration and toggles power.
  // Here, we detect a rising edge of a click by sampling the clickCount logic indirectly.
  // Simpler: we interpret a click by checking for a release that is not long press while in calibration.
  // Implementation detail: we'll reuse the short click handler by checking and advancing in the button evaluation window end.
  // To keep logic simple, we detect a short press release and then if currently in calibration, we advance.
}

static void advanceCalibrationOnShortClick() {
  if (!g_inCalibration) return;
  g_calibIndex++;
  if (g_calibIndex >= 7) {
    // Finished calibration
    g_inCalibration = false;
    saveSettingsToEEPROM();
    // return to powered on state
    if (!g_isOn) {
      g_isOn = true;
      digitalWrite(PIN_PSU_ENABLE, HIGH);
    }
    clearAllTubes();
  }
}

static void showSettingsRate() {
  // Show current updates per second (1..7) by illuminating that many tubes
  for (uint8_t i = 0; i < 7; ++i) {
    setTubeDuty(i, (i < g_updatesPerSecond) ? (uint16_t)(g_tubeMaxDuty[i]) : 0);
  }
}

static void doSettingsStep() {
  // Use gain pot to select 1..7
  int rawV = analogRead(PIN_POT_GAIN);
  float v = 1.0f - (float)rawV / 4095.0f;
  uint8_t sel = (uint8_t)(v * 6.999f) + 1; // 1..7
  if (sel < 1) sel = 1; if (sel > 7) sel = 7;
  g_updatesPerSecond = sel;
  showSettingsRate();
}

static void saveSettingsAndExit() {
  g_inSettings = false;
  saveSettingsToEEPROM();
  clearAllTubes();
}

// ===========================
// UDP discovery & pairing
// ===========================
static void udpSendAnnounce(IPAddress dest) {
  // Packet: 'S','A','A', nameLen(1), name
  char name[32];
  g_deviceName.toCharArray(name, sizeof(name));
  uint8_t nameLen = strnlen(name, sizeof(name));
  uint8_t hdr[4] = {'S','A','A', nameLen};
  udp.beginPacket(dest, UDP_PORT);
  udp.write(hdr, sizeof(hdr));
  udp.write((uint8_t*)name, nameLen);
  udp.endPacket();
}

static void udpBroadcastDiscover() {
  // Packet: 'S','A','Q'
  uint8_t buf[3] = {'S','A','Q'};
  IPAddress bcast(255,255,255,255);
  udp.beginPacket(bcast, UDP_PORT);
  udp.write(buf, sizeof(buf));
  udp.endPacket();
}

static void udpHandle() {
  int psize = udp.parsePacket();
  if (psize <= 0) return;
  IPAddress remote = udp.remoteIP();
  uint8_t buf[64];
  int len = udp.read(buf, sizeof(buf));
  if (len < 3) return;
  // DISCOVER QUERY
  if (buf[0]=='S' && buf[1]=='A' && buf[2]=='Q') {
    // Respond with announce
    udpSendAnnounce(remote);
    return;
  }
  // ANNOUNCE
  if (buf[0]=='S' && buf[1]=='A' && buf[2]=='A' && len >= 4) {
    uint8_t nameLen = buf[3];
    // Store in a temporary discovered list (for simplicity we reuse peers if space)
    bool exists = false;
    for (uint8_t i = 0; i < g_peerCount; ++i) if (g_peers[i] == remote) exists = true;
    if (!exists && g_peerCount < MAX_PEERS) {
      g_peers[g_peerCount++] = remote;
      saveSettingsToEEPROM();
    }
    return;
  }
  // DATA
  if (buf[0]=='S' && buf[1]=='A' && buf[2]=='D' && len >= 11) {
    bool remoteOn = buf[3] != 0;
    uint8_t rbands[7];
    for (uint8_t i = 0; i < 7; ++i) rbands[i] = buf[4+i];
    if (remoteOn && !g_isOn) {
      // Turn on if peer detects audio first
      g_isOn = true;
      digitalWrite(PIN_PSU_ENABLE, HIGH);
    }
    if (g_mirrorMode) {
      // Mirror bands
      memcpy(g_lastBands, rbands, 7);
      if (g_isOn && !g_inCalibration && !g_inSettings) applyBandsToTubes(rbands);
    }
    return;
  }
}

// ===========================
// WiFi connect/AP
// ===========================
static void startAP() {
  String ssid = g_deviceName + "-Setup";
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str());
}

static void connectWiFiOrAP() {
  if (g_lastWifiSsid.length() == 0) {
    startAP();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_lastWifiSsid.c_str(), g_lastWifiPass.c_str());
  uint32_t t0 = nowMs();
  while (WiFi.status() != WL_CONNECTED && (nowMs() - t0) < 15000) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    startAP();
  }
}

// ===========================
// Web UI
// ===========================
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Spectrum Analyser</title>
<style>
:root{--bg:#0b0f14;--fg:#e6eef7;--mut:#9fb3c8;--ac:#4f74ff;--ok:#1ec28b;--warn:#ffb454;--err:#ff5c57}
*{box-sizing:border-box;font-family:ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,Helvetica,Arial}
body{margin:0;background:linear-gradient(180deg,#0b0f14,#0e1520);color:var(--fg)}
.container{max-width:960px;margin:0 auto;padding:24px}
.card{background:#0e1623;border:1px solid #1c2940;border-radius:14px;box-shadow:0 8px 24px rgba(0,0,0,.25);padding:16px;margin-bottom:16px}
h1{font-size:24px;margin:8px 0 16px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}
label{display:block;font-size:12px;color:var(--mut);margin-bottom:6px}
input,select,button{width:100%;padding:10px 12px;background:#0b1220;color:var(--fg);border:1px solid #1c2940;border-radius:10px}
button{cursor:pointer;background:linear-gradient(180deg,#3248a8,#243b88);border:1px solid #2f4dbf}
button:hover{filter:brightness(1.08)}
.row{display:flex;gap:8px;align-items:center}
.badge{display:inline-block;padding:4px 8px;border-radius:999px;background:#0b1220;border:1px solid #1c2940;color:var(--mut);font-size:12px}
.kv{display:flex;justify-content:space-between;margin:8px 0;color:var(--mut)}
.tubes{display:grid;grid-template-columns:repeat(7,1fr);gap:8px;height:120px;padding:8px}
.tube{background:linear-gradient(180deg,#08101a,#0b1220);border:1px solid #1c2940;border-radius:10px;position:relative;overflow:hidden}
.tube>div{position:absolute;bottom:0;left:0;right:0;background:linear-gradient(180deg,#50fa7b,#00d2ff);box-shadow:0 -10px 30px rgba(0,210,255,.25);}
</style>
</head>
<body>
<div class="container">
  <h1>Spectrum Analyser <span id="name" class="badge"></span></h1>
  <div class="card">
    <div class="tubes" id="tubes"></div>
    <div class="kv"><span>Status</span><span id="status">-</span></div>
  </div>
  <div class="grid">
    <div class="card">
      <h3>Device</h3>
      <div class="grid">
        <div>
          <label>Power</label>
          <button id="btnPower">Toggle</button>
        </div>
        <div>
          <label>Mirror Mode</label>
          <select id="mirror">
            <option value="0">Off</option>
            <option value="1">On</option>
          </select>
        </div>
        <div>
          <label>Updates per second (1-7)</label>
          <input type="number" id="ups" min="1" max="7" />
        </div>
        <div>
          <label>Audio threshold (0-255)</label>
          <input type="number" id="thr" min="0" max="255" />
        </div>
      </div>
      <div style="height:8px"></div>
      <button id="save">Save</button>
    </div>
    <div class="card">
      <h3>WiFi</h3>
      <div class="grid">
        <div>
          <label>SSID</label>
          <input id="ssid" />
        </div>
        <div>
          <label>Password</label>
          <input id="pass" type="password" />
        </div>
      </div>
      <div style="height:8px"></div>
      <button id="saveWifi">Save WiFi</button>
      <div style="height:8px"></div>
      <button id="resetWifi" style="background:linear-gradient(180deg,#b33232,#8b2424);border-color:#b33232">Reset WiFi</button>
    </div>
    <div class="card">
      <h3>Peers</h3>
      <div class="row">
        <button id="scan">Scan</button>
        <button id="clearPeers">Clear</button>
      </div>
      <div id="peers" style="margin-top:8px"></div>
    </div>
    <div class="card">
      <h3>Calibration & Settings</h3>
      <div class="row">
        <button id="eraseCal">Erase Calibration</button>
        <button id="eraseCfg">Erase Settings (keep calib)</button>
      </div>
      <small class="mut">Triple-click power within 1s on device to enter calibration. Long-press to enter settings.</small>
    </div>
  </div>
</div>
<script>
const tubes = document.getElementById('tubes');
for(let i=0;i<7;i++){const d=document.createElement('div');d.className='tube';const v=document.createElement('div');v.style.height='0%';d.appendChild(v);tubes.appendChild(d)}
function renderBands(b){[...tubes.children].forEach((t,i)=>{const v=t.firstChild;v.style.height=((b[i]||0)/255*100)+'%'})}
async function getStatus(){const r=await fetch('/api/status');const j=await r.json();document.getElementById('name').textContent=j.name;document.getElementById('status').textContent=j.on?'On':'Off';document.getElementById('mirror').value=j.mirror?1:0;document.getElementById('ups').value=j.ups;document.getElementById('thr').value=j.thr;document.getElementById('ssid').value=j.wifi.ssid||'';renderBands(j.bands||[])}
getStatus();setInterval(getStatus,1000);
btnPower.onclick=()=>fetch('/api/power',{method:'POST'}).then(getStatus)
save.onclick=()=>fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mirror: +document.getElementById('mirror').value===1, ups:+document.getElementById('ups').value, thr:+document.getElementById('thr').value})}).then(getStatus)
saveWifi.onclick=()=>fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:document.getElementById('ssid').value,pass:document.getElementById('pass').value})}).then(()=>alert('Saved. Device will reconnect.'))
resetWifi.onclick=()=>fetch('/api/reset-wifi',{method:'POST'}).then(()=>alert('WiFi reset. Device in AP mode.'))
scan.onclick=async()=>{await fetch('/api/scan',{method:'POST'});setTimeout(loadPeers,600)}
clearPeers.onclick=()=>fetch('/api/peers/clear',{method:'POST'}).then(loadPeers)
async function loadPeers(){const r=await fetch('/api/peers');const j=await r.json();const c=document.getElementById('peers');c.innerHTML='';(j.peers||[]).forEach(ip=>{const d=document.createElement('div');d.textContent=ip;c.appendChild(d)})}
loadPeers();

eraseCal.onclick=()=>fetch('/api/erase-cal',{method:'POST'}).then(()=>alert('Calibration erased.'))
eraseCfg.onclick=()=>fetch('/api/erase-cfg',{method:'POST'}).then(()=>alert('Settings erased (kept calibration).'))
</script>
</body>
</html>
)HTML";

static void handleApiStatus() {
  DynamicJsonDocument doc(1024);
  doc["name"] = g_deviceName;
  doc["on"] = g_isOn;
  doc["mirror"] = g_mirrorMode;
  doc["ups"] = g_updatesPerSecond;
  doc["thr"] = g_audioThreshold;
  JsonArray bands = doc.createNestedArray("bands");
  for (uint8_t i = 0; i < 7; ++i) bands.add(g_lastBands[i]);
  JsonObject wifiObj = doc.createNestedObject("wifi");
  wifiObj["ssid"] = g_lastWifiSsid;
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleApiPower() {
  g_isOn = !g_isOn;
  digitalWrite(PIN_PSU_ENABLE, g_isOn ? HIGH : LOW);
  if (!g_isOn) clearAllTubes();
  server.send(200, "text/plain", "OK");
}

static void handleApiSettings() {
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) { server.send(400, "text/plain", "Bad JSON"); return; }
  g_mirrorMode = doc["mirror"].as<bool>();
  uint8_t ups = doc["ups"].as<uint8_t>();
  if (ups < 1) ups = 1; if (ups > 7) ups = 7;
  g_updatesPerSecond = ups;
  uint8_t thr = doc["thr"].as<uint8_t>();
  g_audioThreshold = thr;
  saveSettingsToEEPROM();
  server.send(200, "text/plain", "OK");
}

static void handleApiWifi() {
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) { server.send(400, "text/plain", "Bad JSON"); return; }
  String ssid = doc["ssid"].as<String>();
  String pass = doc["pass"].as<String>();
  g_lastWifiSsid = ssid; g_lastWifiPass = pass;
  saveSettingsToEEPROM();
  // reconnect
  connectWiFiOrAP();
  server.send(200, "text/plain", "OK");
}

static void handleApiResetWifi() {
  resetWifiCredentials();
  startAP();
  server.send(200, "text/plain", "OK");
}

static void handleApiPeers() {
  DynamicJsonDocument doc(512);
  JsonArray arr = doc.createNestedArray("peers");
  for (uint8_t i = 0; i < g_peerCount; ++i) arr.add(g_peers[i].toString());
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleApiPeersClear() {
  g_peerCount = 0;
  saveSettingsToEEPROM();
  server.send(200, "text/plain", "OK");
}

static void handleApiScan() {
  udpBroadcastDiscover();
  server.send(200, "text/plain", "OK");
}

static void handleApiEraseCal() {
  eraseCalibration();
  server.send(200, "text/plain", "OK");
}

static void handleApiEraseCfg() {
  eraseSettingsKeepCalibration();
  server.send(200, "text/plain", "OK");
}

// ===========================
// Setup & Loop
// ===========================
void setup() {
  // Serial
  Serial.begin(115200);
  delay(50);

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadSettingsFromEEPROM();

  // Name from chip id
  uint64_t mac = ESP.getEfuseMac();
  char id[7]; snprintf(id, sizeof(id), "%06X", (uint32_t)(mac & 0xFFFFFF));
  g_deviceName = String("SA-") + id;

  // Pins
  pinMode(PIN_PSU_ENABLE, OUTPUT);
  digitalWrite(PIN_PSU_ENABLE, LOW);
  pinMode(PIN_POWER_BUTTON, INPUT_PULLUP);

  // LEDC
  ledcInitAll();

  // ADC
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // MSGEQ7
  MSGEQ7.begin();

  // Create audio task on core 0
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 1, &g_audioTaskHandle, 0);

  // WiFi
  connectWiFiOrAP();
  udp.begin(UDP_PORT);

  // Web server
  server.on("/", [](){ server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/power", HTTP_POST, handleApiPower);
  server.on("/api/settings", HTTP_POST, handleApiSettings);
  server.on("/api/wifi", HTTP_POST, handleApiWifi);
  server.on("/api/reset-wifi", HTTP_POST, handleApiResetWifi);
  server.on("/api/peers", HTTP_GET, handleApiPeers);
  server.on("/api/peers/clear", HTTP_POST, handleApiPeersClear);
  server.on("/api/scan", HTTP_POST, handleApiScan);
  server.on("/api/erase-cal", HTTP_POST, handleApiEraseCal);
  server.on("/api/erase-cfg", HTTP_POST, handleApiEraseCfg);
  server.begin();

  // Boot defaults
  g_isOn = false;
  g_lastLoudMs = nowMs();
}

void loop() {
  // Handle web
  server.handleClient();
  // Handle UDP receive
  udpHandle();

  // Pots and buttons on core 1
  updatePots();
  handleButton();

  // Keep PSU on during UI modes
  if (g_inCalibration || g_inSettings) {
    if (!g_isOn) {
      portENTER_CRITICAL(&g_mux);
      g_isOn = true;
      digitalWrite(PIN_PSU_ENABLE, HIGH);
      portEXIT_CRITICAL(&g_mux);
    }
  }

  // UI mode visuals
  if (g_inCalibration) {
    doCalibrationStep();
  }
  if (g_inSettings) {
    doSettingsStep();
  }

  // Mirror/peer notify from core 1 to avoid UDP concurrency with receive
  if (g_peerCount > 0 && !g_mirrorMode) {
    bool sendNow = false;
    uint8_t bands[7];
    portENTER_CRITICAL(&g_mux);
    if (g_newBandsReady) {
      memcpy(bands, g_lastBands, 7);
      g_newBandsReady = false;
      sendNow = true;
    }
    portEXIT_CRITICAL(&g_mux);
    if (sendNow) {
      uint8_t buf[11];
      buf[0] = 'S'; buf[1] = 'A'; buf[2] = 'D';
      buf[3] = g_isOn ? 1 : 0;
      for (uint8_t i = 0; i < 7; ++i) buf[4 + i] = bands[i];
      for (uint8_t i = 0; i < g_peerCount; ++i) {
        udp.beginPacket(g_peers[i], UDP_PORT);
        udp.write(buf, 11);
        udp.endPacket();
      }
    }
  }

  // Normal audio update moved to audio task
}