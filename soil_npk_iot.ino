/*************************************************
   Soil NPK IoT System
   ESP32 + TCS34725 + LCD + Arduino IoT Cloud
*************************************************/

#include <Wire.h>
#include <LiquidCrystal.h>
#include "Adafruit_TCS34725.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>

/************ IOT CLOUD ************/
const char DEVICE_LOGIN_NAME[] = "YOUR_DEVICE_LOGIN_NAME";

const char SSID[]       = "YOUR_WIFI_NAME";
const char PASS[]       = "YOUR_WIFI_PASSWORD";

const char DEVICE_KEY[] = "YOUR_DEVICE_KEY";

/************ IOT VARIABLES ************/
String color;
String status;
String udp;
String userin;

void onColorChange();
void onStatusChange();
void onUdpChange();
void onUserinChange();

void initProperties() {
  ArduinoCloud.setBoardId(DEVICE_LOGIN_NAME);
  ArduinoCloud.setSecretDeviceKey(DEVICE_KEY);

  ArduinoCloud.addProperty(color, READWRITE, ON_CHANGE, onColorChange);
  ArduinoCloud.addProperty(status, READWRITE, ON_CHANGE, onStatusChange);
  ArduinoCloud.addProperty(udp, READWRITE, ON_CHANGE, onUdpChange);
  ArduinoCloud.addProperty(userin, READWRITE, ON_CHANGE, onUserinChange);
}

WiFiConnectionHandler ArduinoIoTPreferredConnection(SSID, PASS);

/************ UDP RX ************/
WiFiUDP udpRx;
const int UDP_PORT = 4210;

bool udpStarted = false;
unsigned long udpDisplayUntil = 0;
const unsigned long UDP_DISPLAY_TIME = 3000;

/************ LCD ************/
LiquidCrystal lcd(2, 15, 19, 18, 5, 4);

/************ TCS34725 ************/
Adafruit_TCS34725 tcs = Adafruit_TCS34725(
  TCS34725_INTEGRATIONTIME_50MS,
  TCS34725_GAIN_4X
);

/************ STORED RESULTS ************/
String N_result = "NOT SET";
String P_result = "NOT SET";
String K_result = "NOT SET";

String N_color = "NOT SET";
String P_color = "NOT SET";
String K_color = "NOT SET";

int N_num = -1;
int P_num = -1;
int K_num = -1;

/************ DECLARATIONS ************/
String detectBasicColor(uint16_t r, uint16_t g, uint16_t b, uint16_t c);
String detectNitrogen(uint16_t r, uint16_t g, uint16_t b, uint16_t c);
String detectPhosphorous(uint16_t r, uint16_t g, uint16_t b, uint16_t c);
String detectPotash(uint16_t r, uint16_t g, uint16_t b, uint16_t c);

String predictSoilStatus(String nVal, String pVal, String kVal);

int nitrogenToNumber(String val);
int phosphorousToNumber(String val);
int potashToNumber(String val);

void processMode(char mode);

void readAveragedSensor(
  uint16_t &rAvg,
  uint16_t &gAvg,
  uint16_t &bAvg,
  uint16_t &cAvg
);

void printNextStep();
void resetTestData();
void showScrollingStatus(String msg);

void startUDP();
void checkUDP();
void showUDPData(String msg);

/************ SETUP ************/
void setup() {

  Serial.begin(9600);
  delay(1500);

  lcd.begin(16, 2);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Soil NPK IoT");

  lcd.setCursor(0, 1);
  lcd.print("Initializing");

  delay(1500);

  initProperties();

  ArduinoCloud.begin(ArduinoIoTPreferredConnection);

  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  if (!tcs.begin()) {

    color = "NO SENSOR";
    status = "TCS34725 Sensor Error";
    udp = "NO SENSOR";

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("TCS34725 Error");

    lcd.setCursor(0, 1);
    lcd.print("Check Sensor");

    while (1) {

      ArduinoCloud.update();
      startUDP();
      checkUDP();

      delay(100);
    }
  }

  color = "WAITING";
  status = "System Ready. Send N from App";
  udp = "UDP Waiting";

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Soil NPK Ready");

  lcd.setCursor(0, 1);
  lcd.print("Send N First");

  delay(2000);

  printNextStep();
}

/************ LOOP ************/
void loop() {

  ArduinoCloud.update();

  startUDP();
  checkUDP();

  if (udpDisplayUntil > 0 && millis() > udpDisplayUntil) {

    udpDisplayUntil = 0;
    printNextStep();
  }

  if (Serial.available()) {

    char ch = toupper(Serial.read());

    while (Serial.available()) {
      Serial.read();
    }

    if (ch == 'N' || ch == 'P' || ch == 'K') {

      if (ch == 'P' && N_result == "NOT SET") {

        color = "Send N First";
        status = "Please send N first";
        return;
      }

      if (ch == 'K' &&
          (N_result == "NOT SET" || P_result == "NOT SET")) {

        color = "Send N/P First";
        status = "Please send N then P first";
        return;
      }

      processMode(ch);
    }
  }
}

/************ UDP FUNCTIONS ************/
void startUDP() {

  if (!udpStarted && WiFi.status() == WL_CONNECTED) {

    udpRx.begin(UDP_PORT);
    udpStarted = true;
    udp = "UDP Ready";
  }
}

void checkUDP() {

  if (!udpStarted) return;

  int packetSize = udpRx.parsePacket();

  if (packetSize <= 0) return;

  char packetBuffer[151];

  int len = udpRx.read(packetBuffer, 150);

  if (len > 0) {
    packetBuffer[len] = '\0';
  }

  String msg = String(packetBuffer);
  msg.trim();

  if (msg.length() == 0) return;

  udp = msg;

  showUDPData(msg);
}

void showUDPData(String msg) {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("UDP Received");

  lcd.setCursor(0, 1);
  lcd.print(msg.substring(0, 16));

  udpDisplayUntil = millis() + UDP_DISPLAY_TIME;
}

/************ IOT CALLBACKS ************/
void onUserinChange() {

  userin.trim();
  userin.toUpperCase();

  if (userin.length() == 0) return;

  char ch = userin.charAt(0);

  if (ch != 'N' && ch != 'P' && ch != 'K') {

    color = "INVALID";
    status = "Invalid input. Send only N / P / K";

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("Invalid Input");

    lcd.setCursor(0, 1);
    lcd.print("Send N/P/K");

    userin = "";

    return;
  }

  if (ch == 'P' && N_result == "NOT SET") {

    color = "Send N First";
    status = "Wrong order. Please send N first";

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("Send N First");

    lcd.setCursor(0, 1);
    lcd.print("Then P and K");

    userin = "";

    return;
  }

  if (ch == 'K' &&
      (N_result == "NOT SET" || P_result == "NOT SET")) {

    color = "Send N/P First";

    status =
      "Wrong order. Please send N first, then P, then K";

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("Send N/P First");

    lcd.setCursor(0, 1);
    lcd.print("Order N P K");

    userin = "";

    return;
  }

  processMode(ch);
}

void onColorChange() {}
void onStatusChange() {}
void onUdpChange() {}

/************ PROCESS MODE ************/
void processMode(char mode) {

  uint16_t r, g, b, c;

  color = "READING " + String(mode);

  status = "Mode " + String(mode) + " Testing...";

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Mode:");
  lcd.print(mode);
  lcd.print(" Testing");

  lcd.setCursor(0, 1);
  lcd.print("Reading...");

  readAveragedSensor(r, g, b, c);

  String basicColor = detectBasicColor(r, g, b, c);

  String npkValue = "MEDIUM";
  int npkNum = 0;

  if (mode == 'N') {

    npkValue = detectNitrogen(r, g, b, c);
    npkNum = nitrogenToNumber(npkValue);

    N_result = npkValue;
    N_num = npkNum;
    N_color = basicColor;

    color = "N: " + basicColor;

    status =
      "Mode N Testing Done | N Color: " +
      basicColor +
      " | N Prediction: " +
      npkValue +
      " | N Value: " +
      String(npkNum);
  }

  else if (mode == 'P') {

    npkValue = detectPhosphorous(r, g, b, c);
    npkNum = phosphorousToNumber(npkValue);

    P_result = npkValue;
    P_num = npkNum;
    P_color = basicColor;

    color = "P: " + basicColor;

    status =
      "Mode P Testing Done | P Color: " +
      basicColor +
      " | P Prediction: " +
      npkValue +
      " | P Value: " +
      String(npkNum);
  }

  else if (mode == 'K') {

    npkValue = detectPotash(r, g, b, c);
    npkNum = potashToNumber(npkValue);

    K_result = npkValue;
    K_num = npkNum;
    K_color = basicColor;

    color = "K: " + basicColor;

    status =
      "Mode K Testing Done | K Color: " +
      basicColor +
      " | K Prediction: " +
      npkValue +
      " | K Value: " +
      String(npkNum);
  }

  Serial.println("--------------------------------------------------");

  Serial.print("Mode: ");
  Serial.println(mode);

  Serial.print("Color: ");
  Serial.println(basicColor);

  Serial.print("R: ");
  Serial.println(r);

  Serial.print("G: ");
  Serial.println(g);

  Serial.print("B: ");
  Serial.println(b);

  Serial.print("C: ");
  Serial.println(c);

  Serial.print("Prediction: ");
  Serial.println(npkValue);

  Serial.print("Value: ");
  Serial.println(npkNum);

  Serial.println("--------------------------------------------------");

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(mode);
  lcd.print(":");
  lcd.print(basicColor.substring(0, 14));

  lcd.setCursor(0, 1);
  lcd.print(npkValue.substring(0, 16));

  delay(2000);

  if (N_result != "NOT SET" &&
      P_result != "NOT SET" &&
      K_result != "NOT SET") {

    String soilStatus =
      predictSoilStatus(N_result, P_result, K_result);

    color =
      "Final N:" + N_color +
      " P:" + P_color +
      " K:" + K_color;

    status =
      "Final NPK Values => N=" + String(N_num) +
      " P=" + String(P_num) +
      " K=" + String(K_num) +
      " | Colors => N:" + N_color +
      " P:" + P_color +
      " K:" + K_color +
      " | Prediction => N:" + N_result +
      " P:" + P_result +
      " K:" + K_result +
      " | Soil Type: " + soilStatus;

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("N:");
    lcd.print(N_num);

    lcd.print(" P:");
    lcd.print(P_num);

    lcd.setCursor(0, 1);
    lcd.print("K:");
    lcd.print(K_num);

    delay(2500);

    showScrollingStatus(soilStatus);

    resetTestData();

    userin = "";

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("Final Result");

    lcd.setCursor(0, 1);
    lcd.print("Check IoT App");
  }

  else {

    userin = "";
    printNextStep();
  }
}

/************ NEXT STEP ************/
void printNextStep() {

  lcd.clear();

  if (N_result == "NOT SET") {

    color = "WAITING N";
    status = "Waiting: Send N from App";

    lcd.setCursor(0, 0);
    lcd.print("Next: N Test");

    lcd.setCursor(0, 1);
    lcd.print("App Send N");
  }

  else if (P_result == "NOT SET") {

    status = "N Done. Send P from App";

    lcd.setCursor(0, 0);
    lcd.print("N Done");

    lcd.setCursor(0, 1);
    lcd.print("Next: P Test");
  }

  else if (K_result == "NOT SET") {

    status = "NP Done. Send K from App";

    lcd.setCursor(0, 0);
    lcd.print("NP Done");

    lcd.setCursor(0, 1);
    lcd.print("Next: K Test");
  }
}

/************ RESET ************/
void resetTestData() {

  N_result = "NOT SET";
  P_result = "NOT SET";
  K_result = "NOT SET";

  N_color = "NOT SET";
  P_color = "NOT SET";
  K_color = "NOT SET";

  N_num = -1;
  P_num = -1;
  K_num = -1;
}

/************ SENSOR READING ************/
void readAveragedSensor(
  uint16_t &rAvg,
  uint16_t &gAvg,
  uint16_t &bAvg,
  uint16_t &cAvg
) {

  unsigned long rSum = 0;
  unsigned long gSum = 0;
  unsigned long bSum = 0;
  unsigned long cSum = 0;

  const int samples = 10;

  delay(1000);

  for (int i = 0; i < samples; i++) {

    ArduinoCloud.update();

    startUDP();
    checkUDP();

    uint16_t r, g, b, c;

    tcs.getRawData(&r, &g, &b, &c);

    rSum += r;
    gSum += g;
    bSum += b;
    cSum += c;

    lcd.setCursor(0, 1);

    lcd.print("Sample ");
    lcd.print(i + 1);
    lcd.print("/");
    lcd.print(samples);
    lcd.print("   ");

    delay(250);
  }

  rAvg = rSum / samples;
  gAvg = gSum / samples;
  bAvg = bSum / samples;
  cAvg = cSum / samples;
}

/************ BASIC COLOR DETECTION ************/
String detectBasicColor(
  uint16_t r,
  uint16_t g,
  uint16_t b,
  uint16_t c
) {

  if (c < 50) return "DARK";

  float sum = r + g + b;

  if (sum <= 0) return "ORANGE";

  float rn = (float)r / sum;

  int rg = (int)r - (int)g;
  int br = (int)b - (int)r;
  int bg = (int)b - (int)g;

  if (abs((int)r - (int)g) < 120 &&
      abs((int)g - (int)b) < 120 &&
      c > 300) {

    return "WHITE/PALE";
  }

  if (b > r && b > g) {

    if (br > 180 && bg > 140) return "DEEP BLUE";
    if (br > 70 && bg > 40) return "BLUE";

    return "LIGHT BLUE";
  }

  if (r > g && r > b) {

    if (r > b && b >= g && rg < 120)
      return "PINK";

    if (g > b) {

      if (rg > 220) return "DEEP BROWN";
      if (rg > 130) return "BROWN";

      return "ORANGE";
    }

    if (rn > 0.42) return "RED";
  }

  if (g > r && g > b) return "GREEN";

  return "ORANGE";
}

/************ NITROGEN MAPPING ************/
String detectNitrogen(
  uint16_t r,
  uint16_t g,
  uint16_t b,
  uint16_t c
) {

  String col = detectBasicColor(r, g, b, c);

  if (col == "WHITE/PALE" || col == "LIGHT BLUE")
    return "VERY LOW";

  if (col == "PINK")
    return "LOW";

  if (col == "ORANGE")
    return "MEDIUM";

  if (col == "RED" ||
      col == "BROWN" ||
      col == "DEEP BROWN")
    return "HIGH";

  return "MEDIUM";
}

/************ PHOSPHOROUS MAPPING ************/
String detectPhosphorous(
  uint16_t r,
  uint16_t g,
  uint16_t b,
  uint16_t c
) {

  String col = detectBasicColor(r, g, b, c);

  if (col == "WHITE/PALE")
    return "VERY LOW";

  if (col == "LIGHT BLUE")
    return "LOW";

  if (col == "BLUE")
    return "MEDIUM";

  if (col == "DEEP BLUE")
    return "HIGH";

  return "MEDIUM";
}

/************ POTASH MAPPING ************/
String detectPotash(
  uint16_t r,
  uint16_t g,
  uint16_t b,
  uint16_t c
) {

  String col = detectBasicColor(r, g, b, c);

  if (col == "WHITE/PALE" || col == "PINK")
    return "VERY LOW";

  if (col == "ORANGE")
    return "LOW";

  if (col == "BROWN")
    return "MEDIUM";

  if (col == "DEEP BROWN" || col == "RED")
    return "HIGH";

  return "MEDIUM";
}

/************ NUMERICAL CONVERSION ************/
int nitrogenToNumber(String val) {

  if (val == "VERY LOW") return 120;
  if (val == "LOW") return 180;
  if (val == "MEDIUM") return 240;
  if (val == "HIGH") return 300;

  return 240;
}

int phosphorousToNumber(String val) {

  if (val == "VERY LOW") return 10;
  if (val == "LOW") return 20;
  if (val == "MEDIUM") return 30;
  if (val == "HIGH") return 40;

  return 30;
}

int potashToNumber(String val) {

  if (val == "VERY LOW") return 80;
  if (val == "LOW") return 140;
  if (val == "MEDIUM") return 200;
  if (val == "HIGH") return 260;

  return 200;
}

/************ SOIL STATUS ************/
String predictSoilStatus(
  String nVal,
  String pVal,
  String kVal
) {

  if (nVal == "HIGH" &&
      pVal == "HIGH" &&
      kVal == "HIGH")
    return "HIGHLY FERTILE SOIL";

  if ((nVal == "MEDIUM" || nVal == "HIGH") &&
      (pVal == "MEDIUM" || pVal == "HIGH") &&
      (kVal == "MEDIUM" || kVal == "HIGH"))
    return "GOOD FERTILE SOIL";

  if (nVal == "VERY LOW" &&
      pVal == "VERY LOW" &&
      kVal == "VERY LOW")
    return "VERY POOR SOIL";

  if (nVal == "LOW" &&
      pVal == "LOW" &&
      kVal == "LOW")
    return "POOR SOIL";

  if (nVal == "LOW" || nVal == "VERY LOW")
    return "NITROGEN DEFICIENT SOIL";

  if (pVal == "LOW" || pVal == "VERY LOW")
    return "PHOSPHOROUS DEFICIENT SOIL";

  if (kVal == "LOW" || kVal == "VERY LOW")
    return "POTASH DEFICIENT SOIL";

  return "MODERATE SOIL";
}

/************ LCD SCROLL ************/
void showScrollingStatus(String msg) {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Soil Status:");

  if (msg.length() <= 16) {

    lcd.setCursor(0, 1);
    lcd.print(msg);

    delay(3000);
  }

  else {

    String scrollText = msg + "    ";

    for (int i = 0;
         i <= scrollText.length() - 1;
         i++) {

      ArduinoCloud.update();

      startUDP();
      checkUDP();

      lcd.setCursor(0, 1);

      String part = scrollText.substring(i);

      if (part.length() < 16) {
        part += "                ";
      }

      lcd.print(part.substring(0, 16));

      delay(350);

      if (i + 16 >= scrollText.length())
        break;
    }

    delay(1000);
  }
}