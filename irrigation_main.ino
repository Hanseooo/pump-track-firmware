// ============================================================
//  Arduino UNO R4 WiFi — IoT Irrigation System
//  Backend: Next.js on Vercel + Upstash Redis
// ============================================================
//
//  QUICK START — READ THIS FIRST:
//
//  This file has TWO operating modes controlled by the flag below.
//
//  ┌─────────────────────────────────────────────────────────┐
//  │  #define TEST_MODE    ← uncomment = test mode (no pump/ │
//  │                         sensor hardware needed)          │
//  │                                                          │
//  │  //#define TEST_MODE  ← comment out = full hardware mode │
//  └─────────────────────────────────────────────────────────┘
//
//  TEST MODE  — What you have now (R4 WiFi + kit relay, no pump/sensor)
//    • Simulates moisture readings (auto-increments or Serial override)
//    • Verifies WiFi connection, API POST/GET, and relay toggle
//    • Relay will click ON/OFF so you can hear/confirm it works
//    • Open Serial Monitor at 115200 baud to watch everything
//    • Type a number 0–100 in Serial Monitor to inject a fake moisture %
//
//  FULL MODE  — When pump + soil sensor arrive
//    • Reads real capacitive soil sensor on A0
//    • Controls real pump via relay on D7
//    • Calibrate SENSOR_DRY / SENSOR_WET before first use (see below)
//
// ============================================================

// ── MODE SELECTOR ─────────────────────────────────────────
#define TEST_MODE          // Comment this line out for full hardware

// ============================================================

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

// ── Pin Definitions ───────────────────────────────────────
#define MOISTURE_PIN  A0   // Capacitive soil sensor analog output
#define RELAY_PIN     7    // Relay module IN pin (active LOW)

// ── Server Settings (overridden by fetchSettings() on boot) ──
int readingIntervalMin = 5;   // Minutes between sensor reads
int dryThreshold       = 40;  // Auto-water if moisture % below this
int pumpDurationSec    = 5;   // How long pump/relay stays ON

// ── Sensor Calibration (FULL MODE only) ───────────────────
//  How to calibrate:
//    1. Temporarily add:  Serial.println(analogRead(A0));  in loop()
//    2. Upload, open Serial Monitor
//    3. Hold sensor in dry air → note the value → set SENSOR_DRY
//    4. Submerge probe tips in water → note the value → set SENSOR_WET
//    5. Remove the temporary Serial.println line
//
//  Typical values for Capacitive Soil Moisture Sensor v1.2:
const int SENSOR_DRY = 820;   // Raw ADC value in open air
const int SENSOR_WET = 380;   // Raw ADC value fully submerged

// ── WiFi & HTTP ───────────────────────────────────────────
WiFiSSLClient wifi;
HttpClient  http(wifi, API_HOST, 443);  // Change 443 → 80 if your host uses HTTP

unsigned long lastReadingTime = 0;

// ── TEST MODE state ───────────────────────────────────────
#ifdef TEST_MODE
  int  fakeMoisture      = 55;   // Starting simulated moisture %
  bool fakeDraining      = true; // Direction of simulation drift
#endif

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1500); // Give Serial Monitor time to open

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // HIGH = relay OFF (active LOW module)

  Serial.println("===========================================");

  #ifdef TEST_MODE
    Serial.println("  MODE: TEST (no pump/sensor hardware)");
    Serial.println("  Tip: type 0–100 in Serial Monitor to");
    Serial.println("       inject a fake moisture reading.");
  #else
    Serial.println("  MODE: FULL HARDWARE");
  #endif

  Serial.println("===========================================");

  connectWiFi();
  delay(3000); 
  fetchSettings(); // Pull threshold/interval/pumpSec from backend
}

// ============================================================
//  MAIN LOOP
// ============================================================
void loop() {
  unsigned long now      = millis();
  unsigned long interval = (unsigned long)readingIntervalMin * 60UL * 1000UL;

  // ── TEST MODE: allow Serial injection of fake readings ──
  #ifdef TEST_MODE
    checkSerialInput();
  #endif

  if (now - lastReadingTime >= interval || lastReadingTime == 0) {
    lastReadingTime = now;

    // 1. Read moisture (real or simulated)
    int moisture = readMoisture();
    Serial.print("[Sensor] Moisture: ");
    Serial.print(moisture);
    Serial.println("%");

    // 2. POST reading to backend
    bool apiOk = sendReading(moisture);

    // 3. If API succeeded → check cloud command queue
    //    If API failed   → local offline fallback
    if (apiOk) {
      checkCommand();
    } else {
      Serial.println("[Fallback] API unreachable — using local threshold logic.");
      if (moisture < dryThreshold) {
        Serial.println("[Fallback] Moisture below threshold → activating relay.");
        activateRelay();
      }
    }
  }
}

// ============================================================
//  MOISTURE READING
// ============================================================
int readMoisture() {
  #ifdef TEST_MODE
    // Simulate a slowly draining / refilling pot
    if (fakeDraining) {
      fakeMoisture -= 3;
      if (fakeMoisture <= 20) fakeDraining = false;
    } else {
      fakeMoisture += 5;
      if (fakeMoisture >= 80) fakeDraining = true;
    }
    fakeMoisture = constrain(fakeMoisture, 0, 100);
    return fakeMoisture;

  #else
    // FULL MODE: real capacitive sensor
    int raw = analogRead(MOISTURE_PIN);
    int pct = map(raw, SENSOR_DRY, SENSOR_WET, 0, 100);
    return constrain(pct, 0, 100);
  #endif
}

// ============================================================
//  POST /api/reading
//  Sends current moisture to backend.
//  Backend stores it in Upstash Redis and returns shouldPump.
//  Returns true if HTTP 2xx received.
// ============================================================
bool sendReading(int moisture) {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  // Build JSON body
  String body = "{\"moisture\":";
  body += moisture;
  body += "}";

  Serial.print("[API] POST /api/reading ... ");

  http.beginRequest();
  http.post("/api/reading");
  http.sendHeader("Content-Type", "application/json");
  http.sendHeader("x-api-key", API_KEY);
  http.sendHeader("Connection", "close");
  http.sendHeader("Content-Length", String(body.length()));
  http.beginBody();
  http.print(body);
  http.endRequest();

  int    status   = http.responseStatusCode();
  String respBody = http.responseBody();

  Serial.println(status);

  if (status >= 200 && status < 300) {
    // Optional: backend may return { ok: true, shouldPump: true }
    // The shouldPump flag is advisory — we always confirm via GET /api/command
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, respBody) == DeserializationError::Ok) {
      bool shouldPump = doc["shouldPump"] | false;
      Serial.print("[API] shouldPump from reading response: ");
      Serial.println(shouldPump ? "true" : "false");
    }
    return true;
  }

  Serial.print("[API] Error body: ");
  Serial.println(respBody);
  return false;
}

// ============================================================
//  GET /api/command
//  Checks Upstash Redis queue for a pending pump command.
//  Commands are consumed on read (prevents double-watering).
// ============================================================
void checkCommand() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  Serial.print("[API] GET /api/command ... ");

  http.beginRequest();
  http.get("/api/command");
  http.sendHeader("x-api-key", API_KEY);
  http.sendHeader("Connection", "close");
  http.endRequest();

  int    status   = http.responseStatusCode();
  String respBody = http.responseBody();

  Serial.println(status);

  if (status == 200) {
    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, respBody);

    if (err) {
      Serial.print("[API] JSON parse error: ");
      Serial.println(err.c_str());
      return;
    }

    bool pump = doc["pump"] | false;
    Serial.print("[API] pump command: ");
    Serial.println(pump ? "true" : "false");

    if (pump) {
      activateRelay();
    }
  } else {
    Serial.print("[API] Error body: ");
    Serial.println(respBody);
  }
}

// ============================================================
//  RELAY / PUMP ACTIVATION
//  In TEST MODE:  relay clicks for pumpDurationSec (no pump needed)
//  In FULL MODE:  relay closes, pump runs, relay opens
// ============================================================
void activateRelay() {
  #ifdef TEST_MODE
    Serial.print("[Relay] TEST — toggling relay ON for ");
    Serial.print(pumpDurationSec);
    Serial.println("s (listen for click)");
  #else
    Serial.print("[Pump] Running pump for ");
    Serial.print(pumpDurationSec);
    Serial.println("s");
  #endif

  digitalWrite(RELAY_PIN, LOW);          // LOW = relay ON (active LOW)
  delay((unsigned long)pumpDurationSec * 1000UL);
  digitalWrite(RELAY_PIN, HIGH);         // HIGH = relay OFF

  #ifdef TEST_MODE
    Serial.println("[Relay] TEST — relay OFF");
  #else
    Serial.println("[Pump] Pump OFF");
  #endif
}

// ============================================================
//  GET /api/settings
//  Fetches operating parameters from backend on startup.
//  Falls back to hardcoded defaults if unreachable.
// ============================================================
void fetchSettings() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  const int maxRetries = 3;
  int status = -1;
  String resp;

  for (int attempt = 1; attempt <= maxRetries; attempt++) {

    Serial.print("[API] GET /api/settings attempt ");
    Serial.println(attempt);

    http.beginRequest();
    http.get("/api/settings");
    http.sendHeader("x-api-key", API_KEY);
    http.sendHeader("Connection", "close");
    http.endRequest();

    status = http.responseStatusCode();
    resp   = http.responseBody();

    Serial.print("[API] status: ");
    Serial.println(status);

    // SUCCESS
    if (status >= 200 && status < 300) {
      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, resp);

      if (!err) {
        readingIntervalMin = doc["intervalMin"] | 5;
        dryThreshold       = doc["threshold"]   | 40;
        pumpDurationSec    = doc["pumpSec"]     | 5;

        Serial.println("[Settings] Loaded from server:");
        Serial.print("intervalMin="); Serial.println(readingIntervalMin);
        Serial.print("threshold=");   Serial.println(dryThreshold);
        Serial.print("pumpSec=");     Serial.println(pumpDurationSec);
      }

      return; // stop retry loop
    }

    // 308 redirect or error → wait and retry
    Serial.print("[Settings] Failed attempt ");
    Serial.println(attempt);

    delay(1500);
  }

  Serial.println("[Settings] All retries failed — using defaults.");
}

// ============================================================
//  WiFi CONNECTION
//  Attempts connection up to 20 times (~10 seconds).
// ============================================================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("[WiFi] Connecting to ");
  Serial.print(WIFI_SSID);
  Serial.print(" ");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" FAILED (will retry on next loop)");
  }
}

// ============================================================
//  TEST MODE ONLY — Serial input for manual moisture injection
//  Open Serial Monitor and type any number 0–100, press Enter.
//  The next reading cycle will use that value instead.
// ============================================================
#ifdef TEST_MODE
void checkSerialInput() {
  if (Serial.available() > 0) {
    int val = Serial.parseInt();
    if (val >= 0 && val <= 100) {
      fakeMoisture = val;
      Serial.print("[TestInput] Moisture overridden to: ");
      Serial.println(fakeMoisture);
      // Force an immediate reading cycle
      lastReadingTime = 0;
    }
    // Flush remaining chars
    while (Serial.available()) Serial.read();
  }
}
#endif
