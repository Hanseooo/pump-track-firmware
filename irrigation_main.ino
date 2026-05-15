// ============================================================
//  Arduino UNO R4 WiFi — IoT Irrigation System
//  Backend: Next.js on Vercel + Upstash Redis
// ============================================================

// ── MODE SELECTOR ─────────────────────────────────────────
// ============================================================
//  Arduino UNO R4 WiFi — IoT Irrigation System
// ============================================================

// #define TEST_MODE

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

// ── Pins ───────────────────────────────────────
#define MOISTURE_PIN  A0
#define RELAY_PIN     7

#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// ── Settings ───────────────────────────────────
int readingIntervalMin = 5;
int dryThreshold       = 40;
int pumpDurationSec    = 5;
int commandPollSec     = 30;

// ── Settings refresh (NEW) ─────────────────────
int settingsRefreshSec = 120;

// ── Sensor calibration ─────────────────────────
const int SENSOR_DRY = 820;
const int SENSOR_WET = 380;

// ── WiFi ──────────────────────────────────────
WiFiSSLClient wifi;
HttpClient http(wifi, API_HOST, 443);

// ── Timers ────────────────────────────────────
unsigned long lastReadingTime  = 0;
unsigned long lastCommandTime  = 0;
unsigned long lastSettingsTime = 0;

// ── Pump state (NON-BLOCKING) ─────────────────
bool pumpActive = false;
unsigned long pumpStartTime = 0;

// ── TEST MODE ─────────────────────────────────
#ifdef TEST_MODE
int  fakeMoisture = 55;
bool fakeDraining = true;
#endif

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  connectWiFi();
  delay(3000);

  fetchSettings();
}

// ============================================================
// LOOP
// ============================================================
void loop() {

#ifdef TEST_MODE
  checkSerialInput();
#endif

  unsigned long now = millis();

  // ── NON-BLOCKING PUMP HANDLER ───────────────
  if (pumpActive) {
    if (now - pumpStartTime >= (unsigned long)pumpDurationSec * 1000UL) {
      digitalWrite(RELAY_PIN, RELAY_OFF);
      pumpActive = false;
      Serial.println("[Pump] OFF (non-blocking)");
    }
  }

  // ── SENSOR READING LOOP ─────────────────────
  unsigned long readingInterval =
    (unsigned long)readingIntervalMin * 60UL * 1000UL;

  if (now - lastReadingTime >= readingInterval || lastReadingTime == 0) {

    lastReadingTime = now;

    int moisture = readMoisture();

    Serial.print("[Sensor] Moisture: ");
    Serial.print(moisture);
    Serial.println("%");

    bool apiOk = sendReading(moisture);

    if (!apiOk && moisture < dryThreshold) {
      Serial.println("[Fallback] Trigger pump");
      startPump();
    }
  }

  // ── COMMAND POLLING LOOP ────────────────────
  unsigned long commandInterval =
    (unsigned long)commandPollSec * 1000UL;

  if (now - lastCommandTime >= commandInterval || lastCommandTime == 0) {

    lastCommandTime = now;
    checkCommand();
  }

  // ── SETTINGS AUTO-REFRESH (NEW) ─────────────
  if (now - lastSettingsTime >= (unsigned long)settingsRefreshSec * 1000UL) {

    lastSettingsTime = now;
    Serial.println("[Settings] Auto-refresh...");
    fetchSettings();
  }
}

// ============================================================
// START PUMP (NON-BLOCKING)
// ============================================================
void startPump() {

  if (pumpActive) {
    Serial.println("[Pump] Already running, skip");
    return;
  }

  Serial.print("[Pump] ON for ");
  Serial.print(pumpDurationSec);
  Serial.println("s (non-blocking)");

  digitalWrite(RELAY_PIN, RELAY_ON);

  pumpActive = true;
  pumpStartTime = millis();
}

// ============================================================
// MOISTURE
// ============================================================
int readMoisture() {

#ifdef TEST_MODE

  if (fakeDraining) {
    fakeMoisture -= 3;
    if (fakeMoisture <= 20) fakeDraining = false;
  } else {
    fakeMoisture += 5;
    if (fakeMoisture >= 80) fakeDraining = true;
  }

  return constrain(fakeMoisture, 0, 100);

#else

  int raw = analogRead(MOISTURE_PIN);
  int pct = map(raw, SENSOR_DRY, SENSOR_WET, 0, 100);
  return constrain(pct, 0, 100);

#endif
}

// ============================================================
// SEND READING
// ============================================================
bool sendReading(int moisture) {

  if (WiFi.status() != WL_CONNECTED)
    connectWiFi();

  String body = "{\"moisture\":" + String(moisture) + "}";

  http.beginRequest();
  http.post("/api/reading");

  http.sendHeader("Content-Type", "application/json");
  http.sendHeader("x-api-key", API_KEY);
  http.sendHeader("Connection", "close");
  http.sendHeader("Content-Length", String(body.length()));

  http.beginBody();
  http.print(body);
  http.endRequest();

  int status = http.responseStatusCode();
  String resp = http.responseBody();

  if (status >= 200 && status < 300) return true;

  Serial.println("[API] sendReading failed");
  return false;
}

// ============================================================
// CHECK COMMAND
// ============================================================
void checkCommand() {

  if (WiFi.status() != WL_CONNECTED)
    connectWiFi();

  http.beginRequest();
  http.get("/api/command");

  http.sendHeader("x-api-key", API_KEY);
  http.sendHeader("Connection", "close");

  http.endRequest();

  int status = http.responseStatusCode();
  String resp = http.responseBody();

  if (status == 200) {

    StaticJsonDocument<128> doc;
    if (!deserializeJson(doc, resp)) {

      bool pump = doc["pump"] | false;

      if (pump) {
        startPump();  // ✅ uses non-blocking version
      }
    }
  }
}

// ============================================================
// SETTINGS
// ============================================================
void fetchSettings() {

  if (WiFi.status() != WL_CONNECTED)
    connectWiFi();

  http.beginRequest();
  http.get("/api/settings");

  http.sendHeader("x-api-key", API_KEY);
  http.sendHeader("Connection", "close");

  http.endRequest();

  int status = http.responseStatusCode();
  String resp = http.responseBody();

  if (status >= 200 && status < 300) {

    StaticJsonDocument<256> doc;

    if (!deserializeJson(doc, resp)) {

      readingIntervalMin = doc["intervalMin"] | 5;
      dryThreshold       = doc["threshold"]   | 40;
      pumpDurationSec    = doc["pumpSec"]     | 5;
      commandPollSec     = doc["commandPollSec"] | 30;

      Serial.println("[Settings] Updated (auto)");
    }
  }
}

// ============================================================
// WIFI
// ============================================================
void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    retries++;
  }
}