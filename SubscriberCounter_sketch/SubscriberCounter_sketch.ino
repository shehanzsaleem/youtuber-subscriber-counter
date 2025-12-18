// ─── Libraries ────────────────────────────────────────────────────────────────
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <Adafruit_GFX.h>
#include <FastLED.h>
#include <FastLED_NeoMatrix.h>

#include <stdlib.h>  // for strtoul

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

// ─── Light Sensor (Ambient) ──────────────────────────────────────────────────
#define PIN_LDR 35

const int LDR_DARK   = 500;   // tune later
const int LDR_BRIGHT = 2600;  // tune later

// Brightness limits (0–255 for FastLED)
const uint8_t MIN_BRIGHTNESS = 1;   // was effectively ~10 before
const uint8_t MAX_BRIGHTNESS = 200; // lower ceiling so bright isn't blinding

uint8_t currentBrightness = 200;

// ─── Icons (8x8 RGB bitmaps) ─────────────────────────────────────────────────
// 💪 Dumbbell gradient (for GYM)
const uint32_t icon_dumbbell_rgb[8][8] = {
  {0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000},
  {0x80D8FF, 0x80D8FF, 0x000000, 0x000000, 0x000000, 0x000000, 0x80D8FF, 0x80D8FF},
  {0x40B0FF, 0x40B0FF, 0x40B0FF, 0x000000, 0x000000, 0x40B0FF, 0x40B0FF, 0x40B0FF},
  {0x000000, 0x0088FF, 0x0088FF, 0x0088FF, 0x0088FF, 0x0088FF, 0x0088FF, 0x000000},
  {0x000000, 0x0060FF, 0x0060FF, 0x0060FF, 0x0060FF, 0x0060FF, 0x0060FF, 0x000000},
  {0x0040C0, 0x0040C0, 0x0040C0, 0x000000, 0x000000, 0x0040C0, 0x0040C0, 0x0040C0},
  {0x002080, 0x002080, 0x000000, 0x000000, 0x000000, 0x000000, 0x002080, 0x002080},
  {0x001040, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x001040}
};

// ❤️ Heart gradient (for TTE)
const uint32_t icon_heart_rgb[8][8] = {
  {0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000},
  {0x000000, 0xFF80C0, 0xFF80C0, 0x000000, 0x000000, 0xFF80C0, 0xFF80C0, 0x000000},
  {0xFF4080, 0xFF4080, 0xFF4080, 0xFF4080, 0xFF4080, 0xFF4080, 0xFF4080, 0xFF4080},
  {0xFF0040, 0xFF0040, 0xFF0040, 0xFF0040, 0xFF0040, 0xFF0040, 0xFF0040, 0xFF0040},
  {0x000000, 0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000, 0x000000},
  {0x000000, 0x000000, 0xD00030, 0xD00030, 0xD00030, 0xD00030, 0x000000, 0x000000},
  {0x000000, 0x000000, 0x000000, 0xA00020, 0xA00020, 0x000000, 0x000000, 0x000000},
  {0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000}
};

// ─── WiFi/HTTPS ───────────────────────────────────────────────────────────────
WiFiClientSecure client;

// ─── Google Sheets Settings ───────────────────────────────────────────────────
const char* SHEET_NAME  = "LED";  // tab name
// NOW: A=Metric, B=Value, C=IconCode, D=TextColor
const char* SHEET_RANGE = "A:D";

unsigned long fetch_interval_ms  = 60UL * 1000UL;
unsigned long rotate_interval_ms = 5UL  * 1000UL;

unsigned long last_fetch_ms  = 0;
unsigned long last_rotate_ms = 0;

#define MAX_ROWS 32
String metrics[MAX_ROWS];
String valuesArr[MAX_ROWS];
String iconCodeArr[MAX_ROWS];   // column C
String colorCodeArr[MAX_ROWS];  // column D
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

// Parse color strings like "0xFF0000", "#FF0000", "FF0000", "red", …
uint16_t parseColorStringTo565(const String &colorStr, uint16_t fallback) {
  String t = colorStr;
  t.trim();
  if (t.length() == 0) return fallback;

  // Some simple named colors for convenience
  if (t.equalsIgnoreCase("red"))     return matrix.Color(255,   0,   0);
  if (t.equalsIgnoreCase("green"))   return matrix.Color(  0, 255,   0);
  if (t.equalsIgnoreCase("blue"))    return matrix.Color(  0,   0, 255);
  if (t.equalsIgnoreCase("white"))   return matrix.Color(255, 255, 255);
  if (t.equalsIgnoreCase("yellow"))  return matrix.Color(255, 255,   0);
  if (t.equalsIgnoreCase("cyan"))    return matrix.Color(  0, 255, 255);
  if (t.equalsIgnoreCase("magenta")) return matrix.Color(255,   0, 255);
  if (t.equalsIgnoreCase("orange"))  return matrix.Color(255, 128,   0);
  if (t.equalsIgnoreCase("pink"))    return matrix.Color(255, 105, 180);

  // Normalise hex style
  if (t.startsWith("#")) {
    t = "0x" + t.substring(1);
  } else if (!t.startsWith("0x") && !t.startsWith("0X")) {
    // if user just puts "FF0000"
    t = "0x" + t;
  }

  const char *cstr = t.c_str();
  char *endptr;
  uint32_t val = (uint32_t)strtoul(cstr, &endptr, 0);
  if (endptr == cstr) return fallback;  // parse failed

  uint8_t r = (val >> 16) & 0xFF;
  uint8_t g = (val >> 8)  & 0xFF;
  uint8_t b =  val        & 0xFF;
  return matrix.Color(r, g, b);
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

  matrix.setCursor(0, 1);
  matrix.print(buf);
  matrix.show();
}

// Metric row with NO icon: metric name on left, value right-aligned, colored
void drawMetricNameAndValue(const String &metric, const String &value, uint16_t color) {
  String m = metric;
  String v = value;
  m.trim();
  v.trim();

  Serial.print("DISPLAY (metric row): ");
  Serial.print(m);
  Serial.print(" | ");
  Serial.println(v);

  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(1);
  matrix.setTextColor(color);

  char metricBuf[32];
  char valueBuf[16];

  m.substring(0, sizeof(metricBuf) - 1).toCharArray(metricBuf, sizeof(metricBuf));
  v.substring(0, sizeof(valueBuf) - 1).toCharArray(valueBuf, sizeof(valueBuf));

  int16_t x1, y1;
  uint16_t wVal, hVal;
  matrix.getTextBounds(valueBuf, 0, 0, &x1, &y1, &wVal, &hVal);

  int16_t xValue = MATRIX_WIDTH - (int16_t)wVal;
  int16_t y      = 1;

  matrix.setCursor(0, y);
  matrix.print(metricBuf);

  matrix.setCursor(xValue, y);
  matrix.print(valueBuf);

  matrix.show();
}

// RGB 8x8 icon + right-aligned value, with colored text
void drawRGBIconAndValue(const uint32_t icon[8][8], const String &value, uint16_t textColor) {
  Serial.print("DISPLAY (RGB icon+value): ");
  Serial.println(value);

  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(1);
  matrix.setTextColor(textColor);

  // Draw RGB icon
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      uint32_t c = icon[y][x];
      if (c != 0x000000) {
        uint8_t r = (c >> 16) & 0xFF;
        uint8_t g = (c >> 8)  & 0xFF;
        uint8_t b =  c        & 0xFF;
        matrix.drawPixel(x, y, matrix.Color(r, g, b));
      }
    }
  }

  // Value text
  char buf[16];
  value.substring(0, sizeof(buf) - 1).toCharArray(buf, sizeof(buf));

  int16_t x1, y1;
  uint16_t w, h;
  matrix.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);

  int16_t x = MATRIX_WIDTH - (int16_t)w;
  int16_t y = 1;

  matrix.setCursor(x, y);
  matrix.print(buf);

  matrix.show();
}

// Parse custom icon string from column C
bool parseIconStringToArray(const String &iconStr, uint32_t out[8][8]) {
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
      out[y][x] = 0x000000;

  if (iconStr.length() == 0) return false;

  String s = iconStr;
  s.replace("\n", " ");
  s.replace("\r", " ");
  s.replace("{", " ");
  s.replace("}", " ");
  s.replace(";", " ");

  int count = 0;
  int start = 0;

  while (start >= 0 && count < 64) {
    int comma = s.indexOf(',', start);
    String token;
    if (comma == -1) {
      token = s.substring(start);
      start = -1;
    } else {
      token = s.substring(start, comma);
      start = comma + 1;
    }

    token.trim();
    if (token.length() == 0) continue;

    const char* cstr = token.c_str();
    char* endptr;
    uint32_t val = (uint32_t) strtoul(cstr, &endptr, 0);

    if (endptr == cstr) continue;

    int y = count / 8;
    int x = count % 8;
    out[y][x] = val;
    count++;
  }

  return (count > 0);
}

void showRow(int idx) {
  if (idx < 0 || idx >= row_count) return;

  String metric   = metrics[idx];
  String value    = valuesArr[idx];
  String iconStr  = iconCodeArr[idx];
  String colorStr = colorCodeArr[idx];

  String metricUpper = metric;
  metricUpper.toUpperCase();

  String valStr = value;
  valStr.trim();
  if (valStr.length() == 0) valStr = "0";

  uint16_t defaultColor = matrix.Color(255, 255, 255);
  uint16_t textColor    = parseColorStringTo565(colorStr, defaultColor);

  // 1) Custom icon from column C
  iconStr.trim();
  if (iconStr.length() > 0) {
    uint32_t customIcon[8][8];
    if (parseIconStringToArray(iconStr, customIcon)) {
      drawRGBIconAndValue(customIcon, valStr, textColor);
      return;
    }
  }

  // 2) Built-in icons
  if (metricUpper == "GYM") {
    drawRGBIconAndValue(icon_dumbbell_rgb, valStr, textColor);
  }
  else if (metricUpper == "TTE") {
    drawRGBIconAndValue(icon_heart_rgb, valStr, textColor);
  }
  else {
    // 3) text-only row
    drawMetricNameAndValue(metric, value, textColor);
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
    String m     = (r.size() >= 1 && !r[0].isNull()) ? String((const char*)r[0]) : "";
    String v     = (r.size() >= 2 && !r[1].isNull()) ? String((const char*)r[1]) : "";
    String icon  = (r.size() >= 3 && !r[2].isNull()) ? String((const char*)r[2]) : "";
    String color = (r.size() >= 4 && !r[3].isNull()) ? String((const char*)r[3]) : "";

    if (m.length() == 0 && v.length() == 0 && icon.length() == 0 && color.length() == 0) continue;

    metrics[row_count]     = m;
    valuesArr[row_count]   = v;
    iconCodeArr[row_count] = icon;
    colorCodeArr[row_count]= color;
    row_count++;
  }

  if (current_index >= row_count) current_index = 0;

  Serial.printf("Fetched %d data rows from sheet.\n", row_count);
  return (row_count > 0);
}

// ─── Auto Brightness from LDR ────────────────────────────────────────────────
void updateBrightnessFromLDR() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  if (now - lastUpdate < 1000) return;  // update once per second
  lastUpdate = now;

  int raw = analogRead(PIN_LDR);
  Serial.print("LDR raw: ");
  Serial.println(raw);

  // Clamp raw reading into expected range
  int clamped = raw;
  if (clamped < LDR_DARK)   clamped = LDR_DARK;
  if (clamped > LDR_BRIGHT) clamped = LDR_BRIGHT;

  // Map LDR to brightness – now with much lower minimum
  int target = map(
    clamped,
    LDR_DARK,   // darkest
    LDR_BRIGHT, // brightest
    MIN_BRIGHTNESS,
    MAX_BRIGHTNESS
  );

  target = constrain(target, MIN_BRIGHTNESS, MAX_BRIGHTNESS);

  // Smoothing (you can make this react faster by changing the weights)
  currentBrightness = (uint8_t)((currentBrightness * 3 + target) / 4);

  Serial.print("Target brightness: ");
  Serial.print(target);
  Serial.print(" | Smoothed: ");
  Serial.println(currentBrightness);

  FastLED.setBrightness(currentBrightness);
  matrix.setBrightness(currentBrightness);
  FastLED.show();
}

// ─── Arduino Setup/Loop ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\nBooting TC001 custom firmware…");

  pinMode(PIN_BUZZER, INPUT_PULLDOWN);
  pinMode(27, INPUT_PULLUP);
  pinMode(26, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);

  FastLED.addLeds<NEOPIXEL, PIN_LED_MATRIX>(matrixleds, NUM_LEDS);
  currentBrightness = 200;
  FastLED.setBrightness(currentBrightness);
  matrix.begin();
  matrix.setBrightness(currentBrightness);

  drawText("HELLO");
  delay(1500);

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

  client.setInsecure();

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

  updateBrightnessFromLDR();

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

  if (row_count > 0 && (now - last_rotate_ms >= rotate_interval_ms)) {
    current_index = (current_index + 1) % row_count;
    showRow(current_index);
    last_rotate_ms = now;
  }
}
