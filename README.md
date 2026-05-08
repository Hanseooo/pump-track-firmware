# PumpTrack Firmware

Arduino UNO R4 WiFi firmware for the PumpTrack IoT irrigation system.

This firmware reads soil moisture data, communicates with the PumpTrack dashboard API, triggers watering through a relay-controlled pump, and supports offline fallback watering logic when the backend is unavailable.

The system is designed for a single-plant automated watering setup using:

- Arduino UNO R4 WiFi
- Capacitive soil moisture sensor
- 5V relay module
- 5V submersible water pump
- Next.js dashboard backend
- Supabase
- Upstash Redis

## Features

- Automatic watering based on moisture threshold
- Manual watering support from the dashboard
- Offline fallback watering logic if API calls fail
- Remote configuration via `/api/settings`
- Moisture logging to backend
- Relay-based pump control
- Test mode without requiring a real sensor or pump
- Serial Monitor debugging output
- WiFi auto-reconnect attempts

---

# Hardware Requirements

## Core Parts

| Component | Spec / Notes | Qty |
| :--- | :--- | :--- |
| Arduino UNO R4 WiFi | Main controller with built-in WiFi | 1 |
| Capacitive Soil Moisture Sensor | Capacitive Sensor v1.2 recommended | 1 |
| 1-Channel Relay Module | 5V active LOW relay | 1 |
| 5V Submersible Mini Pump | Small fountain or aquarium pump | 1 |
| USB Power Bank | 5000mAh+ recommended | 1 |
| USB-A to USB-C Cable | Arduino power cable | 1 |
| Breadboard | Full-size preferred | 1 |
| Jumper Wires | Male-to-male and male-to-female | 1 set |
| Silicone Tubing | Fits pump outlet | 50cm |

> Recommended power bank brands include Anker, Xiaomi, and Baseus models with always-on mode.

## Optional Parts

| Component | Purpose |
| :--- | :--- |
| Water Level Float Sensor | Prevent dry-running |
| DHT22 Sensor | Temperature and humidity readings |
| 16x2 I2C LCD | Local status display |
| Enclosure Box | Splash protection |

---

# Wiring Guide

## Warning

Never connect the water pump directly to Arduino pins.

The pump must be powered through the relay module because Arduino GPIO pins cannot safely supply enough current.

## Pin Reference

| Component | Component Pin | Arduino Pin |
| :--- | :--- | :--- |
| Soil Moisture Sensor | VCC | 3.3V |
| Soil Moisture Sensor | GND | GND |
| Soil Moisture Sensor | AOUT | A0 |
| Relay Module | VCC | 5V |
| Relay Module | GND | GND |
| Relay Module | IN | D7 |
| Relay Module | COM | 5V Power Rail |
| Relay Module | NO | Pump Positive |
| Pump | Positive | Relay NO |
| Pump | Negative | GND Rail |

## Wiring Steps

### 1. Power Rails

Connect the power bank to the breadboard power rails.

- Positive to red rail
- Negative to blue rail

### 2. Soil Moisture Sensor

Connect:

- `VCC` → `3.3V`
- `GND` → `GND`
- `AOUT` → `A0`

### 3. Relay Module

Connect:

- `VCC` → `5V`
- `GND` → `GND`
- `IN` → `D7`
- `COM` → 5V rail
- `NO` → Pump positive wire

### 4. Pump

Connect:

- Pump positive wire → Relay `NO`
- Pump negative wire → GND rail

### 5. Power Arduino

Connect the USB-C cable from the power bank to the Arduino UNO R4 WiFi.

## Common Wiring Mistakes

| Mistake | Symptom | Fix |
| :--- | :--- | :--- |
| Sensor connected to 5V | Unstable readings | Use 3.3V |
| Pump connected directly to Arduino | Resets or hardware damage | Use relay |
| Relay connected to wrong pin | Pump never activates | Use D7 |
| Missing common GND | Invalid sensor readings | Share all grounds |
| Using NC instead of NO | Pump always on | Use NO terminal |

---

# Project Structure

```txt
pump-track-firmware/
├── pump-track-firmware.ino
├── secrets.example.h
├── README.md
└── docs/
```

---

# Arduino IDE Setup

## Software Requirements

- Arduino IDE 2.x
- Arduino UNO R4 WiFi board package
- WiFiS3 library
- ArduinoHttpClient library
- ArduinoJson library

## Install Arduino UNO R4 Board

1. Open Arduino IDE
2. Go to File → Preferences
3. Add:

```txt
https://downloads.arduino.cc/packages/package_index.json
```

4. Open Boards Manager
5. Search for:

```txt
Arduino UNO R4
```

6. Install the board package
7. Select:

```txt
Tools → Board → Arduino UNO R4 WiFi
```

## Install Required Libraries

Install these libraries from Library Manager:

- ArduinoHttpClient
- ArduinoJson

---

# Configuration

Create a file named:

```txt
secrets.h
```

Use `secrets.example.h` as the template.

## Example

```cpp
#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID   "your_wifi_name"
#define WIFI_PASS   "your_wifi_password"

#define API_HOST    "your-app.vercel.app"

#define API_KEY     "your_secret_api_key"

#endif
```

## Important

- Never commit `secrets.h`
- Add `secrets.h` to `.gitignore`
- `API_KEY` must match `ARDUINO_API_KEY` from the dashboard backend

---

# Operating Modes

The firmware supports two operating modes.

## Test Mode

Recommended before connecting the real pump and sensor.

Enabled by default:

```cpp
#define TEST_MODE
```

Features:

- Simulated moisture readings
- Relay click testing
- API request testing
- Serial Monitor moisture override
- No real hardware required

Open Serial Monitor at:

```txt
115200 baud
```

You can type any value from `0-100` into Serial Monitor to simulate moisture readings.

## Full Hardware Mode

Comment out the flag:

```cpp
// #define TEST_MODE
```

Features:

- Real sensor readings from A0
- Real pump activation through relay
- Live irrigation control

---

# Uploading Firmware

1. Connect Arduino UNO R4 WiFi via USB-C
2. Open `.ino` file in Arduino IDE
3. Verify `secrets.h` exists
4. Select correct board and COM port
5. Click Upload

After upload:

1. Open Serial Monitor
2. Set baud rate to `115200`
3. Watch connection logs and moisture readings

---

# Sensor Calibration

Each capacitive sensor behaves slightly differently.

You should calibrate:

```cpp
const int SENSOR_DRY
const int SENSOR_WET
```

## Calibration Steps

1. Upload the firmware
2. Open Serial Monitor
3. Temporarily print raw values:

```cpp
Serial.println(analogRead(A0));
```

4. Record dry-air value
5. Record submerged-water value
6. Update constants in code

Typical values:

```cpp
SENSOR_DRY = 820
SENSOR_WET = 380
```

---

# API Communication

The firmware communicates with the PumpTrack dashboard backend using REST APIs.

## Authentication

Protected endpoints require:

```http
x-api-key: YOUR_ARDUINO_API_KEY
```

## Endpoints

| Method | Route | Purpose |
| :--- | :--- | :--- |
| GET | `/api/settings` | Fetch runtime settings |
| POST | `/api/reading` | Submit moisture reading |
| GET | `/api/command` | Poll for watering command |

## Example Reading Payload

```json
{
  "moisture": 35
}
```

## Example Command Response

```json
{
  "pump": true
}
```

---

# Watering Logic

## Online Mode

When API requests succeed:

1. Read moisture sensor
2. POST reading to backend
3. Poll `/api/command`
4. Activate pump if commanded

## Offline Fallback

If the backend is unreachable:

1. Read moisture sensor
2. Compare against local threshold
3. Activate pump locally if needed

This prevents plants from dying during:

- WiFi outages
- DNS failures
- Backend downtime

---

# Tech Stack

| Layer | Technology |
| :--- | :--- |
| Firmware | Arduino C++ |
| Board | Arduino UNO R4 WiFi |
| Backend | Next.js |
| Database | Supabase |
| Queue / Cache | Upstash Redis |
| Hosting | Vercel |

---

# Related Repository

Main dashboard repository:

```txt
pump-track
```

---

# License

MIT
