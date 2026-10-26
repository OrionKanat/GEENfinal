
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <FFat.h>
#include <TinyGPSPlus.h>
#include <time.h>
#include <ArduinoJson.h>
#include <base64.h>

// Configuration
constexpr int SPECIES_COUNT = 4;
constexpr int buttonPins[4] = {3, 4, 5, 6};//a-d0
constexpr int gpsRxPin = 21; // D6, rx on esp
constexpr int gpsSwitchPin = 7;
const char* nativeSSID = "CU Guest";
const char* nativePassword = NULL;
const String publishableApiKey = "sb_publishable_FRTm-YVmfGFomzD9w5MdZg_iSW_wjRr";
const String secretKey = "SECRET_KEY_HERE"; // Replace with your actual secret key
const String baseUrl = "https://oqmdykrkbfdvvufgazsm.supabase.co";
const String deviceUUID = "5bdc3429-6d45-4c49-81a7-c205ae7e6816";
const bool ENABLE_UPLOADS = true;
const unsigned long BUTTON_DEBOUNCE_MS = 30;
int uploadInterval = 30000;//upload interval in milis
int gpsMaxAge = 5000;//max age of gps fix in milis


enum class Species : int {
  Button1 = 0,
  Button2,
  Button3,
  Button4,
  Invalid = -1
};

struct userSettings {
  String deviceUUID;
  String ssid;
  String password;
  String speciesNames[SPECIES_COUNT] = {
    "Brown Trout", "Rainbow Trout", "Brook Trout", "Cutthroat Trout"
  };
  int uploadInterval;
  bool enableUploads;
  int gpsMaxAge;
};

struct FishEntry {
  Species species = Species::Invalid;
  String time;
  float latitude;
  float longitude;
  String device_id;
};

// Global state
const char* JSONin;
JsonDocument doc;
userSettings userSettingsData;
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
Preferences settings;
const Species speciesForButton[SPECIES_COUNT] = {
  Species::Button1, Species::Button2, Species::Button3, Species::Button4
};
int buttonLastReading[4] = {HIGH, HIGH, HIGH, HIGH};
int buttonState[4] = {HIGH, HIGH, HIGH, HIGH};
unsigned long buttonStateChangedAt[4] = {0};
unsigned long fishCounts[SPECIES_COUNT] = {0};
unsigned long totalRows = 0;
unsigned long uploadedRows = 0;
unsigned long lastUploadAttempt = 0;
bool flashReady = false;
bool syncReady = false;

// Functions
void loadUserSettings(userSettings &settings, JsonDocument &doc) {
  JsonObject wifi = doc["wifi"][0];

  settings.deviceUUID = doc["device_id"] | deviceUUID;
  settings.ssid = wifi["ssid"] | nativeSSID;
  settings.password = wifi["password"] | nativePassword;
  for (int i = 0; i < SPECIES_COUNT; i++) {
    String key = "species" + String(i + 1);
    settings.speciesNames[i] = doc[key] | settings.speciesNames[i];
  }
  settings.uploadInterval = (doc["upload_interval_s"])*1000 | uploadInterval;
  settings.enableUploads = doc["enableUploads"] | ENABLE_UPLOADS;
  settings.gpsMaxAge = doc["gps_max_age_ms"] | gpsMaxAge;
}

int settingsRequest(const String &baseUrl, const JsonDocument &doc) {
String requestUrl = baseUrl + "/rest/v1/get_settings?p_device_id=" + deviceUUID;
if (WiFi.status() != WL_CONNECTED) return -1;

  HTTPClient http;
  http.setTimeout(5000);

  if (!http.begin(requestUrl)) return -1;

  //http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", publishableApiKey);
  //http.addHeader("prefer", "return=minimal");
  http.addHeader("x-device-secret", secretKey);

  int statusCode = http.GET();

  if (statusCode >= 200 && statusCode < 300) {
    String responseBody = http.getString();
    DeserializationError error = deserializeJson(doc, responseBody);
    http.end();

    if (error) {
      Serial.printf("Settings JSON parse failed: %s\n", error.c_str());
      return statusCode;
    }

    return statusCode;
  }

  http.end();
  return statusCode;

  
}

int speciesIndex(Species value) {
  int index = static_cast<int>(value);
  return index >= 0 && index < SPECIES_COUNT ? index : -1;
}

int fishTypeId(Species value) {
  int index = speciesIndex(value);
  return index < 0 ? -1 : index + 1;
}

Species speciesForButtonId(int buttonId) {
  if (buttonId < 1 || buttonId > SPECIES_COUNT) return Species::Invalid;
  return static_cast<Species>(buttonId - 1);
}

const String& speciesName(Species value) {
  static const String unknownSpecies = "Unknown";
  int index = speciesIndex(value);
  if (index < 0) return unknownSpecies;
  return userSettingsData.speciesNames[index];
}

String constructJSON(const FishEntry& entry) {
  return String("{") +
    "\"fish_type_id\": " + String(fishTypeId(entry.species)) + ", " +
    "\"caught_at\": \"" + entry.time + "\", " +
    "\"latitude\": " + String(entry.latitude, 6) + ", " +
    "\"longitude\": " + String(entry.longitude, 6) + ", " +
    "\"device_id\": \"" + entry.device_id + "\"}";
}

int postJson(const String& url, const String& jsonBody, String& responseBody) {
  if (WiFi.status() != WL_CONNECTED) return -1;

  HTTPClient http;
  http.setTimeout(5000);

  if (!http.begin(url)) return -1;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", publishableApiKey);
  http.addHeader("prefer", "return=minimal");
  http.addHeader("x-device-key", secretKey);

  int statusCode = http.POST(jsonBody);

  if (statusCode > 0) responseBody = http.getString();

  http.end();
  return statusCode;
}

String getTime() {
  if (gps.date.isValid() && gps.time.isValid() &&
      gps.date.age() < 5000 && gps.time.age() < 5000) {

    char timestamp[21];
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02dZ",
      gps.date.year(), gps.date.month(), gps.date.day(),
      gps.time.hour(), gps.time.minute(), gps.time.second());

    return String(timestamp);
  }

  time_t now = time(nullptr);
  if (now < 1700000000) return "";

  struct tm timeInfo;
  gmtime_r(&now, &timeInfo);

  char timestamp[21];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeInfo);

  return String(timestamp);
}

FishEntry generateEntry(Species species) {
  FishEntry entry{};
  entry.species = species;
  entry.time = getTime();
  entry.latitude = gps.location.lat();
  entry.longitude = gps.location.lng();
  entry.device_id = deviceUUID;
  return entry;
}

FishEntry generateEntryNoGPS(Species species) {// set coords to varsity lake for demo purposes
  FishEntry entry{};
  entry.species = species;
  entry.time = getTime();
  entry.latitude = 40.009849709769675;
  entry.longitude =  -105.27433817827028;
  entry.device_id = deviceUUID;
  return entry;
}

// Existing CSV: button,count,species,timestamp,gpsFix,latitude,longitude
FishEntry parseLine(String line) {
  FishEntry entry{};

  int commas[6];
  int start = 0;

  for (int i = 0; i < 6; i++) {
    commas[i] = line.indexOf(',', start);
    if (commas[i] == -1) return entry;
    start = commas[i] + 1;
  }

  entry.species = speciesForButtonId(line.substring(0, commas[0]).toInt());

  if (entry.species == Species::Invalid) return entry;

  entry.time = line.substring(commas[2] + 1, commas[3]);

  if (entry.time.isEmpty() || entry.time == "NO_TIME") {
    entry.species = Species::Invalid;
    return entry;
  }

  bool gpsFix = line.substring(commas[3] + 1, commas[4]).toInt() == 1;

  if (!gpsFix) {
    entry.species = Species::Invalid;
    return entry;
  }

  entry.latitude = line.substring(commas[4] + 1, commas[5]).toFloat();
  entry.longitude = line.substring(commas[5] + 1).toFloat();
  entry.device_id = deviceUUID;

  return entry;
}

void loadSavedFish() {
  File file = FFat.open("/fish.csv", "r");
  if (!file) return;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;

    totalRows++;

    int first = line.indexOf(',');
    int second = line.indexOf(',', first + 1);

    if (first < 0 || second < 0) continue;

    Species species = speciesForButtonId(line.substring(0, first).toInt());
    int index = speciesIndex(species);
    if (index < 0) continue;

    unsigned long count = line.substring(first + 1, second).toInt();
    if (count > fishCounts[index]) fishCounts[index] = count;
  }

  file.close();
  Serial.printf("Flash has %lu fish records\n", totalRows);
}

bool saveEntry(const FishEntry& entry, int button) {
  int index = speciesIndex(entry.species);
  if (index < 0) {
    Serial.println("Invalid species; cannot save");
    return false;
  }

  if (!flashReady){
    Serial.println("Flash not ready; cannot save");
   return false;
  }

  File file = FFat.open("/fish.csv", "a");
  if (!file) {
    Serial.println("file not opened");
    return false;
  }

  String line = String(button + 1) + "," + String(fishCounts[index] + 1) + "," +
    speciesName(entry.species) + "," + entry.time + ",1," +
    String(entry.latitude, 6) + "," + String(entry.longitude, 6);

  size_t written = file.println(line);
  file.flush();

  bool saved = written >= line.length() + 1 && !file.getWriteError();
  file.close();
  return saved;
}

void onButtonPress(int button) {
  FishEntry entry;
  if (!gps.location.isValid() || gps.location.age() > 5000) {
    Serial.println("Not saved: waiting for GPS fix");
    Serial.println("We'll fill false coordinates for testing");
    entry = generateEntryNoGPS(speciesForButton[button]);
    if (!saveEntry(entry, button)) return;
    int index = speciesIndex(entry.species);
    fishCounts[index]++;
    totalRows++;
    Serial.printf("Saved %s #%lu | %s | %.6f, %.6f\n",
    speciesName(entry.species).c_str(), fishCounts[index],
    entry.time.c_str(), entry.latitude, entry.longitude);
    return;
  }

  entry = generateEntry(speciesForButton[button]);

  if (entry.time.isEmpty()) {
    Serial.println("Not saved: waiting for time");
    return;
  }

  if (!saveEntry(entry, button)) {
    Serial.println("Not saved: flash write failed");
    return;
  }

  int index = speciesIndex(entry.species);
  fishCounts[index]++;
  totalRows++;

  Serial.printf("Saved %s #%lu | %s | %.6f, %.6f\n",
    speciesName(entry.species).c_str(), fishCounts[index],
    entry.time.c_str(), entry.latitude, entry.longitude);
}

// Store the last acknowledged row so resets don't resend it.
void uploadSavedFish() {
  if (!syncReady || !flashReady || WiFi.status() != WL_CONNECTED) return;

  File file = FFat.open("/fish.csv", "r");
  if (!file) return;

  unsigned long row = 0;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;

    row++;
    if (row <= uploadedRows) continue;

    FishEntry entry = parseLine(line);

    if (entry.species != Species::Invalid) {
      String response;
      int status = postJson(baseUrl + "/rest/v1/mycopy_fish_observations", constructJSON(entry), response);

      if (status < 200 || status >= 300) {
        Serial.printf("Upload failed at row %lu (HTTP %d): %s\n",
          row, status, response.c_str());
        break;
      }
    }

    if (settings.putULong("sentRows", row) == 0) {
      Serial.println("Could not save upload progress");
      syncReady = false;
      break;
    }

    uploadedRows = row;
    Serial.printf("Processed row %lu\n", row);
  }

  file.close();
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    buttonLastReading[i] = digitalRead(buttonPins[i]);
    buttonState[i] = buttonLastReading[i];
    buttonStateChangedAt[i] = millis();
  }

  gpsSerial.begin(9600, SERIAL_8N1, 4, -1);

  flashReady = FFat.begin(false);
  if (flashReady) loadSavedFish();
  else Serial.println("Flash mount failed; records were not erased");

  if (settings.begin("fishsync", false) && deviceUUID != "REPLACE_WITH_DEVICE_UUID") {
    String previousUUID = settings.getString("deviceUUID", "");

    if (previousUUID == deviceUUID || previousUUID.isEmpty()) {
      if (!previousUUID.isEmpty() || settings.putString("deviceUUID", deviceUUID) > 0) {
        uploadedRows = settings.getULong("sentRows", 0);
        syncReady = uploadedRows <= totalRows;
      }
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(userSettingsData.ssid, userSettingsData.password);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.println("Ready: press a fish button");
  if (!ENABLE_UPLOADS) Serial.println("Supabase uploads OFF for testing");
  int status = settingsRequest(baseUrl, doc);
if (status >= 200 && status < 300) {
  loadUserSettings(userSettingsData, doc);
}
}

void loop() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  for (int i = 0; i < 4; i++) {
    int reading = digitalRead(buttonPins[i]);

    if (reading != buttonLastReading[i]) {
      buttonLastReading[i] = reading;
      buttonStateChangedAt[i] = millis();
    }

    if (millis() - buttonStateChangedAt[i] >= BUTTON_DEBOUNCE_MS && reading != buttonState[i]) {
      buttonState[i] = reading;
      if (reading == LOW) onButtonPress(i);
    }
  }

  if (ENABLE_UPLOADS && millis() - lastUploadAttempt >= 30000) {
    lastUploadAttempt = millis();
    uploadSavedFish();
    Serial.printf("Upload attempt complete\n");
  }
}
