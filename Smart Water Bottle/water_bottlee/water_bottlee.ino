#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <RTClib.h>
#include "HX711.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>


const char* ssid      = "";
const char* password  = "";
const char* serverUrl = "http://ip:8000/log_drink";


Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);s

// pins
#define DT_PIN   12   // data
#define SCK_PIN  13   // clock


Adafruit_MPU6050 mpu;
RTC_DS3231 rtc;
HX711 scale;


const long EMPTY_WEIGHT = 453000; // // Calibration 350500, 107530
const long FULL_WEIGHT  = 2380500;
const long CAPACITY_DELTA = FULL_WEIGHT - EMPTY_WEIGHT; 
const float TOTAL_FL_OZ = 33.8;


float smoothedWeight = EMPTY_WEIGHT;
float alpha = 0.08;  // lower = smoother
uint32_t uprightStartTime = 0;
const uint32_t STABILIZE_TIME = 15000;

float totalDrankOz = 0;
float lastUprightWeight = FULL_WEIGHT;
int drinkCount = 0;

bool upright = true;
bool wasUpright = true;


#include <math.h>

bool isUpright(float az) {
  return (az > 5.5);
}



// ===== HTTP send helper =====
void sendDrinkEvent(float amount, int drinkNum, const char* rtcTime) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"amountDrank\": " + String(amount, 2) + ",";
  json += "\"rtcTime\": \"" + String(rtcTime) + "\",";
  json += "\"drinkNumber\": " + String(drinkNum);
  json += "}";

  Serial.println("POSTing: " + json);
  int code = http.POST(json);
  Serial.printf("Server Response: %d\n", code);
  http.end();
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(20);

  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

  Wire.begin();
  Wire.setClock(100000);
  rtc.begin();
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  mpu.begin(0x69);
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  scale.begin(DT_PIN, SCK_PIN);

  Serial.println("Connecting WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nWiFi connected! IP: ");
  Serial.println(WiFi.localIP());

  Serial.println("\nSystem Ready!\n");
}

void loop() {
  DateTime now = rtc.now();

  // Accelerometer
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  upright = isUpright(a.acceleration.z);

  // Load Cell smoothing
  long rawW = scale.is_ready() ? scale.read() : smoothedWeight;
  smoothedWeight = alpha * rawW + (1 - alpha) * smoothedWeight;

  // Convert to ounces
  float currentOz = ((smoothedWeight - EMPTY_WEIGHT) / CAPACITY_DELTA) * TOTAL_FL_OZ;
  currentOz = constrain(currentOz, 0, TOTAL_FL_OZ);
  int percent = (currentOz / TOTAL_FL_OZ) * 100;

  // drinking logic
  static bool pendingDrink = false;

  if (!upright && wasUpright) {
    lastUprightWeight = smoothedWeight;
    pendingDrink = true;
  }

  if (upright && !wasUpright) {
    uprightStartTime = millis();
  }

  if (pendingDrink && upright) {
    if (millis() - uprightStartTime >= STABILIZE_TIME) {
      float change = (lastUprightWeight - smoothedWeight);
      float drinkOz = (change / CAPACITY_DELTA) * TOTAL_FL_OZ;

      if (drinkOz > 0.25) {  // Prevent accidental noise
        drinkCount++;
        totalDrankOz += drinkOz;

        char rtcStr[25];
        sprintf(rtcStr, "%04d-%02d-%02d %02d:%02d:%02d",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());

        sendDrinkEvent(drinkOz, drinkCount, rtcStr);
        Serial.printf("DRINK %d: %.2f oz logged!\n", drinkCount, drinkOz);
      }

      pendingDrink = false;
    }
  }

  wasUpright = upright;
// display
  tft.fillRect(0, 0, 240, 135, ST77XX_BLACK);

  tft.setCursor(10, 10);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_GREEN);
  tft.printf("Drinks: %d", drinkCount);

  tft.setCursor(10, 40);
  tft.printf("Drank: %.1f oz", totalDrankOz);

  tft.setCursor(10, 70);
  tft.printf("Left: %.1f oz", currentOz);

  tft.setCursor(10, 100);
  tft.setTextColor(upright ? ST77XX_CYAN : ST77XX_RED);
  tft.print(upright ? "UPRIGHT" : "TILTED");

  // Progress bar
  int barX = 210, barY = 10, barW = 20, barH = 115;
  tft.fillRect(barX, barY, barW, barH, ST77XX_BLACK);

  int fillH = map(percent, 0, 100, 0, barH);
  int fillY = barY + (barH - fillH);

  tft.fillRect(barX, fillY, barW, fillH, ST77XX_BLUE);
  tft.drawRect(barX, barY, barW, barH, ST77XX_WHITE);

  // Debug
  Serial.printf(
    "%02d:%02d:%02d | Raw=%ld | Oz=%.2f | Drinks=%d | Total=%.2f oz\n",
    now.hour(), now.minute(), now.second(),
    rawW, currentOz, drinkCount, totalDrankOz
  );

  delay(200);
}
