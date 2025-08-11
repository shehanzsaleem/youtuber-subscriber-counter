// ─── Libraries ────────────────────────────────────────────────────────────────
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <HTTPClient.h>
#include <string>

#include <WiFi.h>              // ESP32 Wi-Fi

// Files
#include "secrets.h"
#include "FontSubs.h"

// ─── Matrix Config ────────────────────────────────────────────────────────────
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CS_PIN 5
MD_Parola Display = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// ─── WiFi/HTTPS ───────────────────────────────────────────────────────────────
WiFiClientSecure client;

// ─── Google Sheets Settings ───────────────────────────────────────────────────
const char* SHEET_NAME = "LED";       // change if your tab name is different
const char* SHEET_RANGE = "A:B";      // we expect "Metric" in A and "Value" in B

// Refresh data from Google every X ms, rotate rows every Y ms
unsigned long fetch_interval_ms  = 60UL * 1000UL;  // pull fresh data every 60s
unsigned long rotate_interval_ms = 5UL  * 1000UL;  // show next row every 5s

unsigned long last_fetch_ms  = 0;
unsigned long last_rotate_ms = 0;

// Storage for rows
#define MAX_ROWS 32  // raise if you’ll show more than 32 rows
String metrics[MAX_ROWS];
String valuesArr[MAX_ROWS];
int row_count = 0;        // actual number of populated rows
int current_index = 0;

// JSON doc big enough for a modest sheet
StaticJsonDocument<4096> doc;

// ─── Helpers ──────────────────────────────────────────────────────────────────
String buildSheetsUrl() {
  // Example:
  // https://sheets.googleapis.com/v4/spreadsheets/{id}/values/LED!A:B?key=API_KEY
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

void showRow(int idx) {
  if (idx < 0 || idx >= row_count) return;
  // Compose "METRIC VALUE" (metric uppercased looks nicer on 8x8s)
  String line = metrics[idx];
  line.toUpperCase();
  if (valuesArr[idx].length()) {
    line += " ";
    line += valuesArr[idx];
  }
  Display.displayClear();
  Display.print(line);
}

// Pulls the latest rows from Google Sheets
bool fetchSheetData() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = buildSheetsUrl();
  http.begin(url);
  int code = http.GET();

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

  // Reset storage
  row_count = 0;

  // Expected shape: { "values": [ ["Metric","Value"], ["Gym","32"], ["TTE","26"], ... ] }
  JsonArray rows = doc["values"].as<JsonArray>();
  if (rows.isNull()) {
    Serial.println("No 'values' found in response.");
    return false;
  }

  // Skip header if present
  int startIdx = 0;
  if (rows.size() > 0) {
    JsonArray first = rows[0];
    if (first.size() >= 1 && String((const char*)first[0]).equalsIgnoreCase("Metric")) {
      startIdx = 1;
    }
  }

  for (int i = startIdx; i < rows.size() && row_count < MAX_ROWS; i++) {
    JsonArray r = rows[i];
    String m = (r.size() >= 1 && !r[0].isNull()) ? String((const char*)r[0]) : "";
    String v = (r.size() >= 2 && !r[1].isNull()) ? String((const char*)r[1]) : "";
    // Skip totally empty lines
    if (m.length() == 0 && v.length() == 0) continue;

    metrics[row_count]  = m;
    valuesArr[row_count] = v;
    row_count++;
  }

  // Reset rotation index if needed
  if (current_index >= row_count) current_index = 0;

  Serial.printf("Fetched %d rows from sheet.\n", row_count);
  return (row_count > 0);
}

// ─── Arduino Setup/Loop ───────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);

  Display.begin();
  Display.setIntensity(0);
  Display.setFont(fontSubs);
  Display.setTextAlignment(PA_CENTER);

  Serial.print("Connecting to Wifi...");
  WiFi.begin(SECRET_SSID, SECRET_PASS);
  Display.print(" WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println(WiFi.localIP());
  Display.print("Connected");
  delay(1200);

  client.setInsecure();

  Display.displayClear();
  Display.print("fetching");
  delay(250);

  fetchSheetData();
  last_fetch_ms  = millis();
  last_rotate_ms = millis();

  // If we have data, show the first row
  if (row_count > 0) showRow(current_index);
}

void loop() {
  unsigned long now = millis();

  // Periodic refresh from Google
  if (now - last_fetch_ms >= fetch_interval_ms) {
    fetchSheetData();
    last_fetch_ms = now;
  }

  // Rotate to next row every rotate_interval_ms
  if (row_count > 0 && (now - last_rotate_ms >= rotate_interval_ms)) {
    current_index = (current_index + 1) % row_count;
    showRow(current_index);
    last_rotate_ms = now;
  }
}
