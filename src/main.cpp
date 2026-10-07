#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
// Define the pins for the LEDs
#define LED_TX_PIN 43
#define LED_RX_PIN 44
#define RGB_LED_PIN 48
// Define the number of RGB LEDs (assuming 1 WS2812 LED)
#define NUM_RGB_LEDS 1
// Create an instance of the Adafruit_NeoPixel class
Adafruit_NeoPixel rgb_led = Adafruit_NeoPixel(NUM_RGB_LEDS, RGB_LED_PIN, NEO_GRB +
NEO_KHZ800);
int bPin = 3;
const char* ssid = "CU Guest";//both should be configured thru website
const char* password = NULL;//null for CU guest
const size_t CAPACITY = 300;
const unsigned long BUTTON_DEBOUNCE_MS = 30;
const unsigned long TRIPLE_CLICK_WINDOW_MS = 1000;
struct FishEntry {
  const char* species;
  String time;
  float latitude;
  float longitude;
  int id;
};
FishEntry entries[CAPACITY];//currently stored in ram, needs to be added to flash w/ file system
size_t entryCount = 0;
enum Species{Brown, Rainbow, Brook, Cutthroat};


void blinkRed(){//single blink
  rgb_led.setPixelColor(0, rgb_led.Color(255, 0, 0));
  rgb_led.show();
  delay(500); 
  rgb_led.setPixelColor(0, rgb_led.Color(0, 0, 0));
  rgb_led.show();
  delay(500); 
}
void blinkGreen(){//single blink
  
  rgb_led.setPixelColor(0, rgb_led.Color(0, 255, 0));
  rgb_led.show();
  delay(500); 
  rgb_led.setPixelColor(0, rgb_led.Color(0, 0, 0));
  rgb_led.show();
  delay(500); 

}

const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED: return "connected";
    case WL_NO_SSID_AVAIL: return "SSID not found";
    case WL_CONNECT_FAILED: return "connection failed";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED: return "disconnected";
    default: return "other";
  }
}

const char* speciesName(Species value) {
  switch (value) {
    case Brown:     return "Brown";
    case Rainbow:   return "Rainbow";
    case Brook:     return "Brook";
    case Cutthroat: return "Cutthroat";
  }
  return "Unknown";
}

String getTime() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 10000)) { //fills timeinfo
    Serial.println("Failed to get time from NTP");
    return "";
  }

  char timestamp[21];
  if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeInfo) == 0) {//formats timeinfo into timestamp buffer
    Serial.println("Failed to format timestamp");
    return "";
  }

  return String(timestamp); //returns string version of filled buffer
}

FishEntry generateEntry(){//fake filler entry
    FishEntry entry;
    String time = getTime();
    if (time.isEmpty()) {
      Serial.println("Failed to get time");
      exit(1);
    }
  entry.species = speciesName(Species(random(0, 4)));
  entry.time = time;
  entry.latitude = 0.0; 
  entry.longitude = 0.0;
  entry.id = 1;
  return entry;
}

bool addEntry(FishEntry entry) {
  if (entryCount >= CAPACITY) {
    return false; // Array is full
  }
  entries[entryCount] = entry;
  entryCount++;
  return true;
}

bool onButtonPress(int pressed) {//ressed will represent species
  Serial.println("Button pressed");
  return addEntry(generateEntry());
  //increment physical counter
}

void printEntries() {
  Serial.printf("Stored entries: %u\n", static_cast<unsigned int>(entryCount));
  for (size_t i = 0; i < entryCount; i++) {
    Serial.printf(
      "Entry %u: species=%s, time=%s, lat=%.6f, long=%.6f, id=%d\n",
      static_cast<unsigned int>(i),
      entries[i].species,
      entries[i].time.c_str(),
      entries[i].latitude,
      entries[i].longitude,
      entries[i].id
    );
  }
}

void setup() {//wifi connection code taken from example and modified
  Serial.begin(115200);

  rgb_led.begin();
  rgb_led.clear();
  rgb_led.show();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  Serial.printf("Connecting to Wi-Fi SSID '%s' (open network)\n", ssid);
  WiFi.begin(ssid, password);
  const unsigned long connectionStartedAt = millis();
  unsigned long lastStatusReportAt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    const unsigned long now = millis();
    if (lastStatusReportAt == 0 || now - lastStatusReportAt >= 5000) {
      const wl_status_t status = WiFi.status();
      Serial.printf("Wi-Fi status: %s (%d), elapsed: %lu ms\n",
                    wifiStatusName(status),
                    static_cast<int>(status),
                    now - connectionStartedAt);
      lastStatusReportAt = now;
    }
    blinkRed();
  }
  blinkGreen();
  Serial.println("Connected to WiFi!");
  Serial.printf("IP: %s, MAC: %s, RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(),
                WiFi.macAddress().c_str(),
                WiFi.RSSI());

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  pinMode(bPin, INPUT);
  
}




void loop() {
  if(digitalRead(bPin) == HIGH) {
    onButtonPress(1);
  }
}
