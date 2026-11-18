#include <Wire.h>
#include <LCD_I2C.h>

int rPin = 2;
int gPin = 3;
int yPin = 4;
int bPin = 5;

int brnTrip = 0;
int brnTotal = 0;
int bowTrip = 0;
int bowTotal = 0;
int brkTrip = 0;
int brkTotal = 0;
int cutTrip = 0;
int cutTotal = 0;

int lastPress = 0;

LCD_I2C lcd(0x27, 16, 2);

byte fishLeft[8] = {
  B00000,
  B00001,
  B10011,
  B11111,
  B11111,
  B10011,
  B00001,
  B00000
};

byte fishRight[8] = {
  B00000,
  B11100,
  B11110,
  B11010,
  B11111,
  B11110,
  B11100,
  B00000
};

void drawLabels() {
  lcd.setCursor(0, 0);
  lcd.print(F("BRN BOW"));
  lcd.write(0);
  lcd.write(1);
  lcd.print(F("BRK CUT"));
}

void showTrip(int col, int val) {
  char buf[4];
  if (val < 10) snprintf(buf, sizeof(buf), " %d ", val);
  else if (val < 100) snprintf(buf, sizeof(buf), "%d ", val);
  else snprintf(buf, sizeof(buf), "%d", val);
  lcd.setCursor(col, 1);
  lcd.print(buf);
}

void displayWrite(int id) {
  if (id == 1) {
    brnTrip++;
    brnTotal++;
    showTrip(0, brnTrip);
  } else if (id == 2) {
    bowTrip++;
    bowTotal++;
    showTrip(4, bowTrip);
  } else if (id == 3) {
    brkTrip++;
    brkTotal++;
    showTrip(9, brkTrip);
  } else if (id == 4) {
    cutTrip++;
    cutTotal++;
    showTrip(13, cutTrip);
  }
}

int buttonPress() {
  bool r = !digitalRead(rPin);
  bool g = !digitalRead(gPin);
  bool y = !digitalRead(yPin);
  bool b = !digitalRead(bPin);
  if (r && !g && !y && !b) return 1;
  else if (g && !r && !y && !b) return 2;
  else if (y && !r && !g && !b) return 3;
  else if (b && !r && !g && !y) return 4;
  else return 0;
}

void setup() {
  pinMode(rPin, INPUT);
  pinMode(gPin, INPUT);
  pinMode(yPin, INPUT);
  pinMode(bPin, INPUT);
  Serial.begin(9600);
  
  lcd.begin();
  lcd.clear();
  lcd.backlight();
  lcd.createChar(0, fishLeft);
  lcd.createChar(1, fishRight);
  drawLabels();
  showTrip(0, brnTrip);
  showTrip(4, bowTrip);
  showTrip(9, brkTrip);
  showTrip(13, cutTrip);
  
}

void loop() {

  int press = buttonPress();
  if (press != 0 && lastPress == 0) {
    displayWrite(press);
    Serial.println(press);
    delay(30);
  }
  lastPress = press;
  
}