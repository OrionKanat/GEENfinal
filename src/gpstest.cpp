/*
#include <Arduino.h>
#include <TinyGPS++.h>
constexpr int gpsRxPin = 20; // D7, GPS TX connects here
double lastTestMillis = 0;

TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

void test()
{

    double lat = gps.location.lat();
    double lon = gps.location.lng();
    char timestamp[21];
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
    if (millis() - lastTestMillis > 2500)
    {
        Serial.printf("Latitude: %.6f, Longitude: %.6f, Timestamp: %s, valid: %d, chars: %lu, fixes: %lu, checksum errors: %lu\n",
                      lat, lon, timestamp, gps.location.isValid(),
                      (unsigned long)gps.charsProcessed(),
                      (unsigned long)gps.sentencesWithFix(),
                      (unsigned long)gps.failedChecksum());
        lastTestMillis = millis();
    }
}

void setup()
{
    Serial.begin(115200);
    gpsSerial.begin(9600, SERIAL_8N1, gpsRxPin, -1);
}

void loop()
{
    while (gpsSerial.available())
        gps.encode(gpsSerial.read());
    test();
}
    */