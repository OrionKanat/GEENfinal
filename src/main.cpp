
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <FFat.h>
#include <TinyGPSPlus.h>
#include <time.h>

const int buttonPins[4] = {18, 17, 16, 15};
const char* ssid = "CU Guest";
const char* password = NULL;
const String publishableApiKey = "sb_publishable_RO1TPHGUQlLr4tzOEzjBbw_vU2Hk1ue";
const String url = "https://oqmdykrkbfdvvufgazsm.supabase.co/rest/v1/mycopy_fish_observations";
const String deviceUUID = "REPLACE_WITH_DEVICE_UUID";
const bool ENABLE_UPLOADS = false;
const unsigned long BUTTON_DEBOUNCE_MS = 30;

enum Species {Brown, Rainbow, Brook, Cutthroat};

struct FishEntry {
  int species;
  String time;
  float latitude;
  float longitude;
  String device_id;
};

TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
Preferences settings;

int speciesForButton[4] = {Brown, Rainbow, Brook, Cutthroat};
int buttonLastReading[4] = {HIGH, HIGH, HIGH, HIGH};
int buttonState[4] = {HIGH, HIGH, HIGH, HIGH};
unsigned long buttonStateChangedAt[4] = {0};
unsigned long fishCounts[4] = {0};
unsigned long totalRows = 0;
unsigned long uploadedRows = 0;
unsigned long lastUploadAttempt = 0;
bool flashReady = false;
bool syncReady = false;

const char* speciesName(Species value) {
  switch (value) {
    case Brown: return "Brown";
    case Rainbow: return "Rainbow";
    case Brook: return "Brook";
    case Cutthroat: return "Cutthroat";
  }
  return "Unknown";
}

int speciesFromName(String name) {
  name.trim();
  for (int i = 0; i < 4; i++) {
    if (name == speciesName(static_cast<Species>(i))) return i;
  }
  return -1;
}

String constructJSON(const FishEntry& entry) {
  return String("{") +
    "\"fish_type_id\": " + String(entry.species) + ", " +
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

FishEntry generateEntry(int species) {
  FishEntry entry{};
  entry.species = species;
  entry.time = getTime();
  entry.latitude = gps.location.lat();
  entry.longitude = gps.location.lng();
  entry.device_id = deviceUUID;
  return entry;
}

// Existing CSV: button,count,species,timestamp,gpsFix,latitude,longitude

FishEntry parseLine(String line) {
  FishEntry entry{};
  entry.species = -1;

  int commas[6];
  int start = 0;

  for (int i = 0; i < 6; i++) {
    commas[i] = line.indexOf(',', start);
    if (commas[i] == -1) return entry;
    start = commas[i] + 1;
  }

  String name = line.substring(commas[1] + 1, commas[2]);
  entry.species = speciesFromName(name);

  if (entry.species == -1) return entry;

  entry.time = line.substring(commas[2] + 1, commas[3]);

  if (entry.time.isEmpty() || entry.time == "NO_TIME") {
    entry.species = -1;
    return entry;
  }

  bool gpsFix = line.substring(commas[3] + 1, commas[4]).toInt() == 1;

  if (!gpsFix) {
    entry.species = -1;
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
    int third = line.indexOf(',', second + 1);

    if (first < 0 || second < 0 || third < 0) continue;

    int species = speciesFromName(line.substring(second + 1, third));
    if (species < 0) continue;

    unsigned long count = line.substring(first + 1, second).toInt();
    if (count > fishCounts[species]) fishCounts[species] = count;
  }

  file.close();
  Serial.printf("Flash has %lu fish records\n", totalRows);
}

bool saveEntry(const FishEntry& entry, int button) {
  if (!flashReady) return false;

  File file = FFat.open("/fish.csv", "a");
  if (!file) return false;

  String line = String(button + 1) + "," + String(fishCounts[entry.species] + 1) + "," +
    speciesName(static_cast<Species>(entry.species)) + "," + entry.time + ",1," +
    String(entry.latitude, 6) + "," + String(entry.longitude, 6);

  size_t written = file.println(line);
  file.flush();

  bool saved = written >= line.length() + 1 && !file.getWriteError();
  file.close();
  return saved;
}

void onButtonPress(int button) {
  if (!gps.location.isValid() || gps.location.age() > 5000) {
    Serial.println("Not saved: waiting for GPS fix");
    return;
  }

  FishEntry entry = generateEntry(speciesForButton[button]);

  if (entry.time.isEmpty()) {
    Serial.println("Not saved: waiting for time");
    return;
  }

  if (!saveEntry(entry, button)) {
    Serial.println("Not saved: flash write failed");
    return;
  }

  fishCounts[entry.species]++;
  totalRows++;

  Serial.printf("Saved %s #%lu | %s | %.6f, %.6f\n",
    speciesName(static_cast<Species>(entry.species)), fishCounts[entry.species],
    entry.time.c_str(), entry.latitude, entry.longitude);
}

// Store last acknowledged row so resets don't resend it.
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

    if (entry.species !=-1) {
      String response;
      int status = postJson(url, constructJSON(entry), response);

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
  WiFi.begin(ssid, password);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.println("Ready: press a fish button");
  if (!ENABLE_UPLOADS) Serial.println("Supabase uploads OFF for testing");
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
  }
}
