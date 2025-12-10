// ─── Libraries ────────────────────────────────────────────────────────────────
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <Adafruit_GFX.h>
#include <FastLED.h>
#include <FastLED_NeoMatrix.h>

#include "secrets.h"  // SECRET_SSID, SECRET_PASS, SECRET_SHEETID, SECRET_APIKEY

// ─── Matrix Config for Ulanzi TC001 ──────────────────────────────────────────
#define PIN_BUZZER      15
#define PIN_LED_MATRIX  32

#define MATRIX_WIDTH    32
#define MATRIX_HEIGHT   8
#define NUM_LEDS        (MATRIX_WIDTH * MATRIX_HEIGHT)

CRGB matrixleds[NUM_LEDS];

FastLED_NeoMatrix matrix(
  matrixleds,
  MATRIX_WIDTH, MATRIX_HEIGHT,
  NEO_MATRIX_TOP  + NEO_MATRIX_LEFT +
  NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG
);

// ─── Icons (8x8 bitmaps) ─────────────────────────────────────────────────────
// 1 = lit pixel, 0 = off
// Bits: 0bABCDEFGH where A = leftmost pixel

// 💪 Dumbbell (for GYM)
const uint8_t icon_dumbbell[8] = {
  0b00000000, // empty top row
  0b11000011,
  0b11100111,
  0b01111110,
  0b01111110,
  0b11100111,
  0b11000011,
  0b10000001
};

// ❤️ Heart (for TTE)
const uint8_t icon_heart[8] = {
  0b00000000, // empty top row
  0b01100110, // two bumps of the heart
  0b11111111, // full width
  0b11111111, // full width
  0b01111110, // tapering down
  0b00111100, // taper
  0b00011000, // sharper bottom
  0b00000000  // clean finish
};

// ─── WiFi/HTTPS ───────────────────────────────────────────────────────────────
WiFiClientSecure client;

// ─── Google Sheets Settings ───────────────────────────────────────────────────
const char* SHEET_NAME  = "LED";  // tab name
const char* SHEET_RANGE = "A:B";  // "Metric" in col A, "Value" in col B

unsigned long fetch_interval_ms  = 60UL * 1000UL;  // pull fresh data every 60s
unsigned long rotate_interval_ms = 5UL  * 1000UL;  // show next row every 5s

unsigned long last_fetch_ms  = 0;
unsigned long last_rotate_ms = 0;

#define MAX_ROWS 32
String metrics[MAX_ROWS];
String valuesArr[MAX_ROWS];
int row_count     = 0;
int current_index = 0;

StaticJsonDocument<4096> doc;

// ─── Helpers ──────────────────────────────────────────────────────────────────
String buildSheetsUrl() {
  String url = "https://sheets.googleapis.com/v4/spreadsheets/";
  url += SECRET_SHEETID;
  url += "/values/";
  url += SHEET_NAME;
  url += "!";
  url += SHEET_RANGE;
  url += "?key=";
  url += SECRET_APIKEY;
  return url;
}

// Status / generic text, left-ish
void drawText(const String &s) {
  Serial.print("DISPLAY (text): ");
  Serial.println(s);

  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(1);
  matrix.setTextColor(matrix.Color(255, 255, 255));

  char buf[64];
  s.substring(0, sizeof(buf) - 1).toCharArray(buf, sizeof(buf));

  matrix.setCursor(0, 1);  // y=1 looks nicer on 8px height
  matrix.print(buf);
  matrix.show();
}

// Right-aligned text for metric fallback rows (no icon)
void drawRightAlignedMetricText(const String &s, uint16_t color) {
  Serial.print("DISPLAY (metric text): ");
  Serial.println(s);

  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(1);
  matrix.setTextColor(color);

  char buf[64];
  s.substring(0, sizeof(buf) - 1).toCharArray(buf, sizeof(buf));

  // Measure width
  int16_t x1, y1;
  uint16_t w, h;
  matrix.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);

  int16_t x = MATRIX_WIDTH - (int16_t)w; // right-aligned
  int16_t y = 1;                         // same baseline as numbers

  matrix.setCursor(x, y);
  matrix.print(buf);
  matrix.show();
}

// Draw 8x8 icon on the left, and a value right-aligned on the right
void drawIconAndValue(const uint8_t icon[8], uint16_t color, const String &value) {
  Serial.print("DISPLAY (icon+value): ");
  Serial.println(value);

  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(1);
  matrix.setTextColor(color);

  // Draw 8x8 icon at far left (x=0..7)
  for (int y = 0; y < 8; y++) {
    uint8_t row = icon[y];
    for (int x = 0; x < 8; x++) {
      if (row & (1 << (7 - x))) { // highest bit = leftmost pixel
        matrix.drawPixel(x, y, color);
      }
    }
  }

  // Prepare value string
  char buf[16];
  value.substring(0, sizeof(buf) - 1).toCharArray(buf, sizeof(buf));

  // Measure text width
  int16_t x1, y1;
  uint16_t w, h;
  matrix.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);

  // Right-align the value against the far right of the 32px matrix
  int16_t x = MATRIX_WIDTH - (int16_t)w; // right aligned
  int16_t y = 1;                         // fixed Y so we don't only see underscores

  matrix.setCursor(x, y);
  matrix.print(buf);

  matrix.show();
}

void showRow(int idx) {
  if (idx < 0 || idx >= row_count) return;

  String metric = metrics[idx];
  String value  = valuesArr[idx];

  metric.toUpperCase();

  // For icons we only care about the numeric/value string
  String valStr = value;
  valStr.trim();

  if (valStr.length() == 0) {
    valStr = "0";  // fallback
  }

  // Metric-based icon & colour, NO "GYM"/"TTE" text, just icon + value
  if (metric == "GYM") {
    // Blue dumbbell
    drawIconAndValue(icon_dumbbell, matrix.Color(0, 0, 255), valStr);
  }
  else if (metric == "TTE") {
    // Red heart
    drawIconAndValue(icon_heart, matrix.Color(255, 0, 0), valStr);
  }
  else {
    // Fallback: plain white text, right-aligned
    String line = metric;
    if (value.length()) {
      line += " ";
      line += value;
    }
    drawRightAlignedMetricText(line, matrix.Color(255, 255, 255));
  }
}

// Pulls latest rows from Google Sheets
bool fetchSheetData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Not connected to WiFi, skipping fetch.");
    return false;
  }

  HTTPClient http;
  String url = buildSheetsUrl();
  Serial.print("Requesting: ");
  Serial.println(url);

  http.begin(url);
  int code = http.GET();

  Serial.print("HTTP status: ");
  Serial.println(code);

  if (code <= 0) {
    Serial.printf("HTTP error: %d\n", code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(err.f_str());
    return false;
  }

  row_count = 0;

  JsonArray rows = doc["values"].as<JsonArray>();
  if (rows.isNull()) {
    Serial.println("No 'values' array in response.");
    return false;
  }

  // Skip header if first row is "Metric"
  int startIdx = 0;
  if (rows.size() > 0) {
    JsonArray first = rows[0];
    if (first.size() >= 1 && !first[0].isNull()
        && String((const char*)first[0]).equalsIgnoreCase("Metric")) {
      startIdx = 1;
    }
  }

  for (int i = startIdx; i < rows.size() && row_count < MAX_ROWS; i++) {
    JsonArray r = rows[i];
    String m = (r.size() >= 1 && !r[0].isNull()) ? String((const char*)r[0]) : "";
    String v = (r.size() >= 2 && !r[1].isNull()) ? String((const char*)r[1]) : "";

    if (m.length() == 0 && v.length() == 0) continue;

    metrics[row_count]   = m;
    valuesArr[row_count] = v;
    row_count++;
  }

  if (current_index >= row_count) current_index = 0;

  Serial.printf("Fetched %d data rows from sheet.\n", row_count);
  return (row_count > 0);
}

// ─── Arduino Setup/Loop ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\nBooting TC001 custom firmware…");

  // TC001 quirks: turn off buzzer, enable buttons
  pinMode(PIN_BUZZER, INPUT_PULLDOWN);
  pinMode(27, INPUT_PULLUP);  // middle button
  pinMode(26, INPUT_PULLUP);  // left button
  pinMode(14, INPUT_PULLUP);  // right button

  // LED matrix init
  FastLED.addLeds<NEOPIXEL, PIN_LED_MATRIX>(matrixleds, NUM_LEDS);
  FastLED.setBrightness(255);  // max brightness (FastLED side)
  matrix.begin();
  matrix.setBrightness(255);   // max brightness (NeoMatrix/Adafruit_GFX side)

  drawText("HELLO");   // sanity check
  delay(1500);

  // WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(SECRET_SSID);
  WiFi.begin(SECRET_SSID, SECRET_PASS);
  drawText("WIFI...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi OK, IP: ");
  Serial.println(WiFi.localIP());
  drawText("CONNECTED");
  delay(1200);

  client.setInsecure(); // HTTPS without cert validation

  drawText("FETCHING");
  delay(250);

  bool ok = fetchSheetData();
  last_fetch_ms  = millis();
  last_rotate_ms = millis();

  if (ok && row_count > 0) {
    showRow(current_index);
  } else {
    drawText("NO DATA");
  }
}

void loop() {
  unsigned long now = millis();

  // Periodic refresh from Google
  if (now - last_fetch_ms >= fetch_interval_ms) {
    bool ok = fetchSheetData();
    last_fetch_ms = now;

    if (ok && row_count > 0) {
      current_index = 0;
      showRow(current_index);
    } else {
      drawText("NO DATA");
    }
  }

  // Rotate to next row
  if (row_count > 0 && (now - last_rotate_ms >= rotate_interval_ms)) {
    current_index = (current_index + 1) % row_count;
    showRow(current_index);
    last_rotate_ms = now;
  }
}
