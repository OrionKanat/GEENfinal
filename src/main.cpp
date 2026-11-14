

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <FFat.h>
#include <TinyGPS++.h>
#include <time.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

// Configuration
constexpr int SPECIES_COUNT = 4;
constexpr int buttonPins[4] = {2, 3, 4, 5};//a-d0
constexpr int gpsRxPin = 20; // D7, rx on esp
const char* defaultSSID = "CU Guest";
const char* defaultPassword = NULL;
const String publishableApiKey = "sb_publishable_FRTm-YVmfGFomzD9w5MdZg_iSW_wjRr";
const String secretKey = "69956812af394b9fa65cfd6cf85debaea25724b67fdb47beb1f0eea851e02659"; // Replace with your actual secret key
const String baseUrl = "https://oqmdykrkbfdvvufgazsm.supabase.co";
const String deviceUUID = "5bdc3429-6d45-4c49-81a7-c205ae7e6816";//make uuid generate
const bool ENABLE_UPLOADS = true;
const unsigned long BUTTON_DEBOUNCE_MS = 300;
int defaultUploadInterval = 30000;//upload interval in milis
int defaultGpsMaxAge = 5000;//max age of gps fix in milis
bool gpsHasfix = false;
int pendingButton = -1;
bool gpsFixWaitInProgress = false;
const unsigned long GPS_FIX_WAIT_TIMEOUT_MS = 5000;


enum class Species : int {
  Button1 = 0,
  Button2,
  Button3,
  Button4,
  Invalid = -1
};

struct userSettings {
  String deviceUUID = deviceUUID;
  String ssid = defaultSSID;
  String password = defaultPassword;
  String speciesNames[SPECIES_COUNT] = {
    "Brown Trout", "Rainbow Trout", "Brook Trout", "Cutthroat Trout"
  };
  int uploadInterval = defaultUploadInterval;
  int gpsMaxAge = defaultGpsMaxAge;
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
int buttonState[4] = {HIGH, HIGH, HIGH, HIGH};
unsigned long lastButtonPressAt[4] = {0};
unsigned long fishCounts[SPECIES_COUNT] = {0};
unsigned long totalRows = 0;
unsigned long uploadedRows = 0;
bool flashReady = false;
bool syncReady = false;
bool buttonWakeupReady = false;
bool wifiSyncPending = false;
unsigned long wifiSyncStartedAt = 0;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;

// Functions


void loadUserSettings(userSettings& settings, JsonDocument& doc) {
  JsonObject wifi = doc["wifi"][0];

  settings.deviceUUID = doc["device_id"] | deviceUUID;
  settings.ssid = wifi["ssid"] | defaultSSID;
  settings.password = wifi["password"] | defaultPassword;
  for (int i = 0; i < SPECIES_COUNT; i++) {
    String key = "species" + String(i + 1);
    settings.speciesNames[i] = doc[key] | settings.speciesNames[i];
  }
  settings.uploadInterval = (doc["upload_interval_s"] | (defaultUploadInterval / 1000)) * 1000UL;
  settings.gpsMaxAge = doc["gps_max_age_ms"] | defaultGpsMaxAge;
}

bool isWifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool connectWifi(unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(userSettingsData.ssid.c_str(), userSettingsData.password.c_str());

  unsigned long wifiStartedAt = millis();
  while (!isWifiConnected() && millis() - wifiStartedAt < timeoutMs) {
    delay(100);
  }

  return isWifiConnected();
}

void disconnectWifi() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

void beginWifiConnection() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(userSettingsData.ssid.c_str(), userSettingsData.password.c_str());
}

bool configureButtonWakeup() {
  esp_err_t result = esp_sleep_enable_gpio_wakeup();
  if (result != ESP_OK) {
    Serial.printf("Could not enable GPIO light-sleep wakeup (%d)\n", result);
    return false;
  }

  for (int i = 0; i < 4; i++) {
    result = gpio_wakeup_enable(
      static_cast<gpio_num_t>(buttonPins[i]), GPIO_INTR_LOW_LEVEL);
    if (result != ESP_OK) {
      Serial.printf("Could not enable wakeup for button GPIO %d (%d)\n",
        buttonPins[i], result);
      return false;
    }
  }

  return true;
}

bool buttonsReleased() {
  for (int i = 0; i < 4; i++) {
    if (buttonState[i] != HIGH || digitalRead(buttonPins[i]) != HIGH) return false;
  }
  return true;
}

void queueButtonPress(int button) {
  unsigned long now = millis();
  if (now - lastButtonPressAt[button] < BUTTON_DEBOUNCE_MS) return;

  lastButtonPressAt[button] = now;
  if (pendingButton == -1) {
    pendingButton = button;
  } else {
    Serial.printf("Ignoring button %d while another press is pending\n", button + 1);
  }
}

void captureWokenButton() {
  uint64_t wakeStatus = esp_sleep_get_gpio_wakeup_status();
  int wakeButton = -1;

  for (int i = 0; i < 4; i++) {
    if (wakeStatus & (1ULL << buttonPins[i])) {
      buttonState[i] = LOW;
      if (wakeButton == -1) wakeButton = i;
    }
  }

  if (wakeButton == -1) {
    for (int i = 0; i < 4; i++) {
      if (digitalRead(buttonPins[i]) == LOW) {
        buttonState[i] = LOW;
        wakeButton = i;
        break;
      }
    }
  }

  if (wakeButton >= 0) queueButtonPress(wakeButton);
}

void pollButtonPresses() {
  for (int i = 0; i < 4; i++) {
    int reading = digitalRead(buttonPins[i]);
    if (reading == HIGH) {
      buttonState[i] = HIGH;
    } else if (buttonState[i] == HIGH) {
      buttonState[i] = LOW;
      queueButtonPress(i);
    }
  }
}

bool waitForFreshFix(unsigned long timeoutMs) {
  gpsFixWaitInProgress = true;
  gpsHasfix = false;

  while (gpsSerial.available()) gpsSerial.read();
  (void)gps.location.lat();
  (void)gps.date.value();
  (void)gps.time.value();

  unsigned long startedAt = millis();
  while (millis() - startedAt < timeoutMs) {
    while (gpsSerial.available()) gps.encode(gpsSerial.read());
    pollButtonPresses();

    if (gps.location.isUpdated() && gps.location.isValid() &&
        gps.date.isUpdated() && gps.date.isValid() &&
        gps.time.isUpdated() && gps.time.isValid()) {
      gpsHasfix = true;
      break;
    }

    delay(10);
  }

  gpsFixWaitInProgress = false;
  return gpsHasfix;
}

void enterLightSleep() {
  if (wifiSyncPending || pendingButton != -1 || gpsFixWaitInProgress ||
      !buttonsReleased()) return;

  disconnectWifi();
  if (!buttonWakeupReady) return;

  esp_err_t result = esp_light_sleep_start();
  captureWokenButton();
  if (result != ESP_OK) {
    Serial.printf("Light sleep failed (%d)\n", result);
    buttonWakeupReady = false;
  }
}

int settingsRequest(const String &baseUrl, JsonDocument &doc) {
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
void testButtons(){
  static unsigned long lastPrintedAt = 0;
  if (millis() - lastPrintedAt < 200) return;
  lastPrintedAt = millis();

  for (int i = 0; i < SPECIES_COUNT; i++) {
    Serial.printf("Button %d state: %s\n", i + 1,
      digitalRead(buttonPins[i]) == HIGH ? "HIGH" : "LOW");
      

    //if(digitalRead(buttonPins[i]) == LOW) {
      //Serial.printf("Button %d pressed. Species is %s\n", i + 1, speciesName(speciesForButton[i]).c_str());
   }
   Serial.printf("________________________________\n");
  }


String constructJSON(const FishEntry& entry) {
  return String("{") +
    "\"device_id\": \"" + deviceUUID + "\", " + 
    "\"fish_type_id\": " + String(fishTypeId(entry.species)) + ", " +
    "\"caught_at\": \"" + entry.time + "\", " +
    "\"latitude\": " + String(entry.latitude, 6) + ", " +
    "\"longitude\": " + String(entry.longitude, 6) + ", " +
    "\"device_id\": \"" + entry.device_id + "\"}";
}
String constructJSON(const FishEntry& entry, const String& recordId) {
  JsonDocument payload;
  payload["device_id"] = entry.device_id;

  JsonArray catches = payload["catches"].to<JsonArray>();
  JsonObject catchRecord = catches.add<JsonObject>();
  catchRecord["fish_type_id"] = fishTypeId(entry.species);
  catchRecord["caught_at"] = entry.time;
  catchRecord["latitude"] = entry.latitude;
  catchRecord["longitude"] = entry.longitude;
  catchRecord["device_record_id"] = recordId;

  String json;
  serializeJson(payload, json);
  return json;
}

int postJson(const String& url, const String& jsonBody, String& responseBody) {
  const bool connected = isWifiConnected();
  const String ipAddress = connected ? WiFi.localIP().toString() : String("unavailable");
  const String macAddress = WiFi.macAddress();
  Serial.printf("HTTP debug: Wi-Fi connected=%s, IP=%s, MAC=%s\n",
    connected ? "true" : "false", ipAddress.c_str(), macAddress.c_str());
  if (!connected) {
    Serial.println("HTTP debug: skipping POST because Wi-Fi is disconnected");
    return -1;
  }

  HTTPClient http;
  http.setTimeout(5000);

  if (!http.begin(url)) {
    Serial.println("HTTP debug: http.begin failed");
    return -1;
  }
http.addHeader("Content-Type", "application/json");
http.addHeader("apiKey", publishableApiKey);
//http.addHeader("apikey", publishableApiKey);
//http.addHeader("Authorization", String("Bearer ") + userAccessToken);
//http.addHeader("Accept", "application/json");

http.addHeader("x-device-key", secretKey);  


  Serial.printf("POST %s\n", url.c_str());
  Serial.printf("Request JSON: %s\n", jsonBody.c_str());

  


  int statusCode = http.POST(jsonBody);

  if (statusCode > 0) responseBody = http.getString();
  else Serial.printf("HTTP debug: POST failed (%d): %s\n",
    statusCode, http.errorToString(statusCode).c_str());

  http.end();
  return statusCode;
}

String getTime() {
  if (gps.date.isValid() && gps.time.isValid() &&
      gps.date.age() < userSettingsData.gpsMaxAge && gps.time.age() < userSettingsData.gpsMaxAge) {

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
  String time = getTime();
  entry.species = species;
  entry.time = time.isEmpty() ? "NO_TIME" : time;
  entry.latitude = gps.location.lat();
  entry.longitude = gps.location.lng();
  entry.device_id = deviceUUID;
  return entry;
}
/*
FishEntry generateEntryNoGPS(Species species) {// set coords to varsity lake for demo purposes
  FishEntry entry{};
  entry.species = species;
   String time = getTime() =="" ? "NO_TIME" : getTime();
  entry.time = time;
  entry.latitude = 40.006860;
  entry.longitude =  -105.261467;
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
  if (!FFat.exists("/fish.csv")) return;

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

bool clearMem() {
  if (!flashReady) {
    Serial.println("Cannot clear saved fish: FFat is not mounted");
    return false;
  }

  if (FFat.exists("/fish.csv") && !FFat.remove("/fish.csv")) {
    Serial.println("Could not delete /fish.csv");
    return false;
  }

  if (settings.putULong("sentRows", 0) == 0) {
    Serial.println("Could not reset upload progress; saved fish file was deleted");
    syncReady = false;
    return false;
  }

  for (int i = 0; i < SPECIES_COUNT; i++) {
    fishCounts[i] = 0;
  }
  totalRows = 0;
  uploadedRows = 0;
  syncReady = true;

  Serial.println("Deleted all locally saved fish and reset upload progress");
  return true;
}

bool saveEntry(const FishEntry& entry, bool gpsFix) {
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

  String line = String(fishTypeId(entry.species)) + "," + String(fishCounts[index] + 1) + "," +
    speciesName(entry.species) + "," + entry.time + "," + (gpsFix ? "1," : "0,") +
    String(entry.latitude, 6) + "," + String(entry.longitude, 6);

  size_t written = file.println(line);
  file.flush();

  bool saved = written >= line.length() + 1 && !file.getWriteError();
  file.close();
  return saved;
}

void onButtonPress(int button) {
  FishEntry entry;
  /*
  if (!gps.location.isValid() || gps.location.age() > userSettingsData.gpsMaxAge) {
    Serial.println("Not saved: waiting for GPS fix");
    Serial.println("We'll fill false coordinates for testing");
    entry = generateEntryNoGPS(speciesForButton[button]);
    if (!saveEntry(entry)) return;
    int index = speciesIndex(entry.species);
    fishCounts[index]++;
    totalRows++;
    Serial.printf("Saved %s #%lu | %s | %.6f, %.6f\n",
    speciesName(entry.species).c_str(), fishCounts[index],
    entry.time.c_str(), entry.latitude, entry.longitude);
    return;
  }
    
  entry = generateEntry(speciesForButton[button]);

  if (entry.time.isEmpty() || entry.time == "NO_TIME") {
    Serial.println("ERROR: catch not saved because no valid timestamp is available");
    return;
  }

  if (!gpsHasfix) {
    Serial.printf("WARNING: saving %s with last-known coordinates and gpsFix=0; it will be skipped during upload\n",
      speciesName(entry.species).c_str());
  }

  if (!saveEntry(entry, gpsHasfix)) {
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
  if (!syncReady || !flashReady || WiFi.status() != WL_CONNECTED || !FFat.exists("/fish.csv")) return;
  File file = FFat.open("/fish.csv", "r");
  if (!file) return;

  unsigned long row = 0;
  unsigned long newRecordsUploaded = 0;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;

    row++;
    if (row <= uploadedRows) continue;

    FishEntry entry = parseLine(line);
    String recordId = deviceUUID + "-" + entry.time + "-" + String(fishTypeId(entry.species));
    if (entry.species != Species::Invalid) {
      String response;
      int status = postJson(baseUrl + "/functions/v1/upload-catches", constructJSON(entry, recordId), response);

      if (status < 200 || status >= 300) {
        Serial.printf("Upload failed at row %lu (HTTP %d): %s\n",
          row, status, response.c_str());
        break;
      }

      newRecordsUploaded++;
      Serial.printf("New records uploaded this run: %lu\n", newRecordsUploaded);
    }

    if (settings.putULong("sentRows", row) == 0) {
      Serial.println("Could not save upload progress");
      syncReady = false;
      break;
    }

    // This is a processed-row cursor; no-fix rows are skipped, not uploaded.
    uploadedRows = row;
    Serial.printf("Processed row %lu\n", row);
  }

  file.close();
}

void setup() {
  Serial.begin(115200);
 

  for (int i = 0; i < 4; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    buttonState[i] = HIGH;
  }
  buttonWakeupReady = configureButtonWakeup();

  gpsSerial.begin(9600, SERIAL_8N1, gpsRxPin, -1);

  bool settingsReady = settings.begin("fishsync", false);
  bool ffatInitialized = settingsReady && settings.getBool("ffatInit", false);
  flashReady = FFat.begin(false);
  if (!flashReady && settingsReady && !ffatInitialized) {
    if (settings.putBool("ffatInit", true) > 0) {
      ffatInitialized = true;
      Serial.println("Initializing FFat for first use");
      flashReady = FFat.begin(true);
    } else {
      Serial.println("Could not record FFat initialization; automatic formatting disabled");
    }
  }

  if (flashReady && settingsReady && !ffatInitialized) {
    if (settings.putBool("ffatInit", true) == 0) {
      FFat.end();
      flashReady = false;
      Serial.println("Could not save FFat initialization marker; unmounted to protect saved records");
    } else {
      ffatInitialized = true;
    }
  }

  if (flashReady) loadSavedFish();
  else Serial.println("Flash mount failed; automatic formatting is disabled to protect saved records");

  if (settingsReady && deviceUUID != "REPLACE_WITH_DEVICE_UUID") {
    String previousUUID = settings.getString("deviceUUID", "");

    if (previousUUID == deviceUUID || previousUUID.isEmpty()) {
      if (!previousUUID.isEmpty() || settings.putString("deviceUUID", deviceUUID) > 0) {
        uploadedRows = settings.getULong("sentRows", 0);
        syncReady = uploadedRows <= totalRows;
      }
    }
  }

  userSettingsData.ssid = defaultSSID;
  userSettingsData.password = defaultPassword ? defaultPassword : "";

  Serial.println("Ready: press a fish button");
  if (!ENABLE_UPLOADS) Serial.println("Supabase uploads OFF for testing");
  if (connectWifi(WIFI_CONNECT_TIMEOUT_MS)) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    int status = settingsRequest(baseUrl, doc);
    if (status >= 200 && status < 300) {
      loadUserSettings(userSettingsData, doc);
    } else {
      Serial.printf("Settings request failed (HTTP %d)\n", status);
    }
  } else {
    Serial.println("WiFi connection failed; using built-in settings");
  }
  disconnectWifi();
 // clearMem();
    
}

void loop() {

 //testButtons();
  
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  pollButtonPresses();

  bool buttonHandled = false;
  if (pendingButton >= 0) {
    int button = pendingButton;
    pendingButton = -1;
    waitForFreshFix(GPS_FIX_WAIT_TIMEOUT_MS);
    onButtonPress(button);
    buttonHandled = true;
  }

  if (buttonHandled && ENABLE_UPLOADS && syncReady && flashReady &&
      totalRows > uploadedRows && !wifiSyncPending) {
    wifiSyncPending = true;
    wifiSyncStartedAt = millis();
    beginWifiConnection();
  }

  if (wifiSyncPending && isWifiConnected()) {
    uploadSavedFish();
    wifiSyncPending = false;
  } else if (wifiSyncPending &&
      millis() - wifiSyncStartedAt >= WIFI_CONNECT_TIMEOUT_MS) {
    Serial.println("WiFi connection failed; queued fish will upload after a later button press");
    wifiSyncPending = false;
  }

  if (wifiSyncPending) delay(10);
  enterLightSleep();
}

