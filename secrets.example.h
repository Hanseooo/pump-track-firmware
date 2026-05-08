// secrets.example.h — copy this to secrets.h and fill in your values
// This file IS safe to commit. secrets.h is NOT.

#ifndef SECRETS_H
#define SECRETS_H

// ── WiFi Credentials ──────────────────────────────────────
#define WIFI_SSID   "MyNetwork"
#define WIFI_PASS   "MyPassword"

// ── Backend API ────────────────────────────────────────────
// Do NOT include "https://" or a trailing slash
#define API_HOST    "your-app.vercel.app"

// Must match ARDUINO_API_KEY in your Vercel environment variables
#define API_KEY     "replace_with_your_actual_key"

#endif // SECRETS_H