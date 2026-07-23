#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>   // ArduinoJson v7.x - install from Library Manager
#include "secrets.h"       // needs: ssid, password

// =============================================================
// LOCATION - CHANGE THIS TO YOUR LOCATION
// =============================================================
// Find your coordinates at https://open-meteo.com/en/docs (map picker)
// or just look them up on Google Maps.
const float LATITUDE = 37.3646;
const float LONGITUDE = -120.4245;

// Timezone must be a valid IANA timezone name, URL-encoded
// ("/" becomes "%2F"). Find yours at:
// https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
const char* TIMEZONE_ENCODED = "America%2FLos_Angeles";

const int FORECAST_DAYS = 3; // how many days of hourly forecast to pull

// =============================================================
// WEATHER VARIABLES - CHANGE WHAT YOU FETCH
// =============================================================
// These are comma-separated lists of Open-Meteo variable names.
// Full list of available variables (there are many more than this):
// https://open-meteo.com/en/docs
//
// To add/remove a variable: edit the string, then update the matching
// print function below (printForecast / printCurrentWeather) to read it.
const char* HOURLY_VARS  = "temperature_2m,precipitation_probability,surface_pressure,wind_speed_10m,wind_direction_10m,soil_temperature_6cm";
const char* CURRENT_VARS = "temperature_2m,is_day,precipitation,weather_code,cloud_cover,wind_speed_10m,wind_direction_10m";

// =============================================================
// PIN CONFIG
// =============================================================
const int BOOT_BUTTON = 0; // built-in BOOT button on most ESP32 dev boards

// =============================================================
// TIMING CONFIG - CHANGE HOW OFTEN THINGS HAPPEN
// =============================================================
const unsigned long DEBOUNCE_MS = 100;
const unsigned long BUTTON_COOLDOWN_MS = 2000;          // min gap between forecast requests
const unsigned long CURRENT_WEATHER_INTERVAL_MS = 5UL * 60UL * 1000UL; // 5 minutes

// =============================================================
// STATE VARIABLES - internal, generally don't need to change
// =============================================================
int lastRawButtonState = HIGH;
int buttonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long lastButtonPress = 0;
unsigned long lastCurrentFetch = 0;

// =============================================================
// WIFI CONNECTION
// =============================================================
void connectWifi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); // blocking is fine here - nothing else to do without WiFi
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

// =============================================================
// URL BUILDING
// =============================================================
// Built from the LATITUDE/LONGITUDE/etc constants above so you only
// have to change values in one place, not hunt through a long URL string.
String buildForecastURL() {
  String url = "https://api.open-meteo.com/v1/forecast?";
  url += "latitude=" + String(LATITUDE, 4);
  url += "&longitude=" + String(LONGITUDE, 4);
  url += "&hourly=" + String(HOURLY_VARS);
  url += "&timezone=" + String(TIMEZONE_ENCODED);
  url += "&forecast_days=" + String(FORECAST_DAYS);
  return url;
}

String buildCurrentURL() {
  String url = "https://api.open-meteo.com/v1/forecast?";
  url += "latitude=" + String(LATITUDE, 4);
  url += "&longitude=" + String(LONGITUDE, 4);
  url += "&current=" + String(CURRENT_VARS);
  url += "&timezone=" + String(TIMEZONE_ENCODED);
  url += "&forecast_days=" + String(FORECAST_DAYS);
  return url;
}

// =============================================================
// HTTP GET + JSON PARSE HELPER
// =============================================================
// NOTE: unlike the MQTT project, this call BLOCKS while it connects and
// downloads - there's no getting around that with HTTPClient, since a
// GET request is a one-shot request/response, not an always-on
// connection like MQTT. It typically takes well under a second on WiFi,
// so it's not a practical problem here, just worth understanding why
// this sketch isn't "non-blocking" in the same way the MQTT one was.
//
// Returns true and fills `doc` on success, false on failure.
bool fetchJson(const String& url, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure(); // skip cert validation - fine for class use, not for production

  HTTPClient http;
  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.print("HTTP GET failed, code: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  // Parse directly from the response stream instead of loading the
  // whole body into a String first - saves memory on larger responses
  // like the multi-day hourly forecast.
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return false;
  }

  return true;
}

// =============================================================
// PRINTING - CHANGE THESE IF YOU CHANGE HOURLY_VARS / CURRENT_VARS
// =============================================================
void printForecast() {
  JsonDocument doc;
  Serial.println("Fetching forecast...");

  if (!fetchJson(buildForecastURL(), doc)) {
    Serial.println("Failed to get forecast.");
    return;
  }

  JsonArray time      = doc["hourly"]["time"];
  JsonArray temp      = doc["hourly"]["temperature_2m"];
  JsonArray precipPct = doc["hourly"]["precipitation_probability"];
  JsonArray pressure  = doc["hourly"]["surface_pressure"];
  JsonArray windSpeed = doc["hourly"]["wind_speed_10m"];
  JsonArray windDir   = doc["hourly"]["wind_direction_10m"];
  JsonArray soilTemp  = doc["hourly"]["soil_temperature_6cm"];

  Serial.println("---- Hourly Forecast ----");
  for (size_t i = 0; i < time.size(); i++) {
    Serial.print(time[i].as<const char*>());
    Serial.print(" | temp: "); Serial.print(temp[i].as<float>());
    Serial.print("C | precip: "); Serial.print(precipPct[i].as<int>());
    Serial.print("% | pressure: "); Serial.print(pressure[i].as<float>());
    Serial.print("hPa | wind: "); Serial.print(windSpeed[i].as<float>());
    Serial.print("km/h @ "); Serial.print(windDir[i].as<int>());
    Serial.print("deg | soil temp: "); Serial.print(soilTemp[i].as<float>());
    Serial.println("C");
  }
  Serial.println("-------------------------");

  // TO ADD A VARIABLE:
  //   1. Add its name to HOURLY_VARS up top
  //   2. Add a line here: JsonArray myVar = doc["hourly"]["your_variable_name"];
  //   3. Print it inside the loop above: myVar[i].as<float>()
}

void printCurrentWeather() {
  JsonDocument doc;
  Serial.println("Fetching current weather...");

  if (!fetchJson(buildCurrentURL(), doc)) {
    Serial.println("Failed to get current weather.");
    return;
  }

  JsonObject current = doc["current"];

  Serial.println("---- Current Weather ----");
  Serial.print("Time: ");        Serial.println(current["time"].as<const char*>());
  Serial.print("Temp: ");        Serial.print(current["temperature_2m"].as<float>()); Serial.println("C");
  Serial.print("Is day: ");      Serial.println(current["is_day"].as<int>() ? "yes" : "no");
  Serial.print("Precip: ");      Serial.print(current["precipitation"].as<float>()); Serial.println("mm");
  Serial.print("Weather code: "); Serial.println(current["weather_code"].as<int>());
  Serial.print("Cloud cover: "); Serial.print(current["cloud_cover"].as<int>()); Serial.println("%");
  Serial.print("Wind speed: ");  Serial.print(current["wind_speed_10m"].as<float>()); Serial.println("km/h");
  Serial.print("Wind dir: ");    Serial.print(current["wind_direction_10m"].as<int>()); Serial.println("deg");
  Serial.println("-------------------------");

  // weather_code is a WMO code (0 = clear, 1-3 = partly cloudy, etc.)
  // Full table: https://open-meteo.com/en/docs#weathervariables
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  connectWifi();

  // grab an initial current-weather reading right away rather than
  // waiting the full 5 minutes for the first one
  printCurrentWeather();
  lastCurrentFetch = millis();
}

void loop() {
  // -----------------------------------------------------------
  // BUTTON - non-blocking debounce with edge detection (same
  // pattern as the MQTT project). Triggers the 3-day hourly
  // forecast print.
  // -----------------------------------------------------------
  int reading = digitalRead(BOOT_BUTTON);

  if (reading != lastRawButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        unsigned long now = millis();
        if (now - lastButtonPress >= BUTTON_COOLDOWN_MS) {
          printForecast();
          lastButtonPress = now;
        }
      }
    }
  }

  lastRawButtonState = reading;

  // -----------------------------------------------------------
  // CURRENT WEATHER - fetched automatically every
  // CURRENT_WEATHER_INTERVAL_MS, independent of the button.
  // -----------------------------------------------------------
  if (millis() - lastCurrentFetch >= CURRENT_WEATHER_INTERVAL_MS) {
    printCurrentWeather();
    lastCurrentFetch = millis();
  }
}
