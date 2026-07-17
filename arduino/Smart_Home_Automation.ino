#include "BluetoothSerial.h"
#include <Wire.h>
#include "DHT.h"
#include <U8g2lib.h>
#include <Ticker.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#if !defined(CONFIG_BT_ENABLED) ||
!defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run make menuconfig to and enable
it
#endif
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Bluetooth not available or not enabled. It is only available for
the ESP32 chip.
#endif
#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;#endif
/* Function Prototypes */
void tempRead(void *parameter);
void autoFan(void *parameter);
void lightRead(void *parameter);
void autoLight(void *parameter);
void smokeDetect(void *parameter);
void buttonDetect(void *parameter);
void ultrasonicDetect();
void switchControl(void *parameter);
void indicatorDisplay(void *parameter);
void introDisplay();
/* Sensor and Input pins */
#define DHTPIN 33
#define DHTTYPE DHT11
#define lightSensor 26
#define smokeSensor 25
#define manualButton 32 // Push button connected to VCC
#define echo 2
#define trigger 15
/* Relay pins (Active LOW) */
#define fanRelay 17
#define lightRelay 16/* Buzzer pins */
#define smokeBuzzer 14
/* Led pins */
#define smokeLed 5
#define ultrasonicLed 18
/* Defining objects */
DHT dht(DHTPIN, DHTTYPE);
// U8g2 constructor for SH1106 128x64 display (I2C default pins: SDA=21,
SCL=22)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*
reset=*/ U8X8_PIN_NONE);
BluetoothSerial SerialBT;
Ticker ultrasonic;
/* Defining queues */
static QueueHandle_t tempReading;
static QueueHandle_t lightReading;
/* Defining task handles */
TaskHandle_t autoFan_handle = NULL;
TaskHandle_t autoLight_handle = NULL;
/* Global State Variables */
bool fanStatus = false;
bool lightStatus = false;
bool smokeStatus = false;bool ultrasonicStatus = false;
// NEW: Global flag/timestamp for light manual override (0 means auto is
active)
static TickType_t lightManualOverrideUntil = 0;
// The initial state for autoLight is MANUAL override at startup
static bool autoLightModeActive = false;
//
======================================================
===========================================
// SETUP
//
======================================================
===========================================
void setup() {
WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
Serial.begin(115200);
Wire.begin();
SerialBT.begin("ESP32");
Serial.println("The device started, now you can pair it with bluetooth!");
dht.begin();
ultrasonic.attach(1, ultrasonicDetect);
u8g2.begin(); // Initialize U8g2 library
/* Defining pin modes */pinMode(fanRelay, OUTPUT);
pinMode(lightRelay, OUTPUT);
pinMode(smokeLed, OUTPUT);
pinMode(ultrasonicLed, OUTPUT);
pinMode(smokeBuzzer, OUTPUT);
pinMode(trigger, OUTPUT);
pinMode(echo, INPUT);
// Set up the new push button with internal pull-down resistor
pinMode(manualButton, INPUT_PULLDOWN);
/* Initialize all actuators OFF (Relays are active LOW) */
digitalWrite(fanRelay, HIGH);
digitalWrite(lightRelay, HIGH);
/* Buzzers and Leds off at start */
digitalWrite(smokeBuzzer, LOW);
digitalWrite(smokeLed, LOW);
digitalWrite(ultrasonicLed, LOW);
/* OLED display intro sequence */
introDisplay();
/* Creating queues */
tempReading = xQueueCreate(5, sizeof(int));
lightReading = xQueueCreate(5, sizeof(int));/* Creating tasks */
xTaskCreatePinnedToCore (tempRead, "Temp read", 2048, NULL, 1,
NULL, app_cpu);
xTaskCreatePinnedToCore (autoFan, "Auto fan", 2048, NULL, 1,
&autoFan_handle, app_cpu);
xTaskCreatePinnedToCore (lightRead, "Light read", 1024, NULL, 1,
NULL, app_cpu);
xTaskCreatePinnedToCore (autoLight, "Auto light", 1024, NULL, 1,
&autoLight_handle, app_cpu);
xTaskCreatePinnedToCore (smokeDetect, "Smoke detect", 1024, NULL, 1,
NULL, app_cpu);
xTaskCreatePinnedToCore (buttonDetect, "Button read", 1024, NULL, 1,
NULL, app_cpu);
xTaskCreatePinnedToCore (switchControl, "Switch control", 4096, NULL,
1, NULL, app_cpu);
xTaskCreatePinnedToCore (indicatorDisplay, "OLED display", 4096,
NULL, 1, NULL, app_cpu);
/* Suspending auto fan mode at start */
vTaskSuspend (autoFan_handle);
// AutoLight is not suspended; its behavior is controlled by the override
flag.
}
//
======================================================
===========================================
// MAIN LOOP
//
======================================================
===========================================void loop() {
vTaskDelay(500 / portTICK_PERIOD_MS);
}
//
======================================================
===========================================
// SENSOR AND CONTROL TASKS
//
======================================================
===========================================
void tempRead(void *parameter) {
int t = 0;
while (true) {
t = dht.readTemperature();
if (isnan(t) || t < -10 || t > 60) {
Serial.println(F("DHT Read Error or Out of Range. Retrying..."));
} else {
SerialBT.print("#"); SerialBT.print(t); SerialBT.print("?");
Serial.print("Temperature: "); Serial.print(t); Serial.println(" °C");
if (xQueueSend (tempReading, (void*)&t, 0) != pdPASS) {
Serial.println("Warning: Temp Queue Full!");
}
}
vTaskDelay(2000 / portTICK_PERIOD_MS);}
}
void autoFan(void *parameter) {
int tempValue;
while (true) {
if (xQueueReceive(tempReading, (void *)&tempValue,
portMAX_DELAY) == pdPASS) {
if (tempValue >= 33) {
if (!fanStatus) SerialBT.print ("Fan on?");
digitalWrite(fanRelay,LOW);
fanStatus = true;
}
else if (tempValue < 33) {
if (fanStatus) SerialBT.print ("Fan off?");
digitalWrite(fanRelay,HIGH);
fanStatus = false;
}
}
vTaskDelay(200 / portTICK_PERIOD_MS);
}
}
void lightRead(void *parameter) {
int lightValue;
while (true) {
lightValue = analogRead(lightSensor);
Serial.print("Light intensity: "); Serial.println(lightValue);if (xQueueSend (lightReading, (void*)&lightValue, 0) != pdPASS) {
Serial.println("Warning: Light Queue Full!");
}
vTaskDelay(2000 / portTICK_PERIOD_MS);
}
}
/**
* REPAIRED TASK: Now checks the global override flag
(lightManualOverrideUntil).
*/
void autoLight(void *parameter) {
int lightValue;
TickType_t currentTicks = 0;
while (true) {
currentTicks = xTaskGetTickCount();
// Check if manual override is active
if (currentTicks < lightManualOverrideUntil) {
if (autoLightModeActive) {
Serial.println("AutoLight: Manual override active. Waiting...");
autoLightModeActive = false;
}
vTaskDelay(500 / portTICK_PERIOD_MS); // Check again soon
continue;
}if (!autoLightModeActive) {
Serial.println("AutoLight: Resuming automatic control.");
autoLightModeActive = true;
}
// Automatic control logic only runs if the override has expired
if (xQueueReceive(lightReading, (void *)&lightValue,
portMAX_DELAY) == pdPASS) {
if (lightValue <= 2200) { // Light ON when it's dark (low ADC value)
if (!lightStatus) SerialBT.print("Bulb on?");
digitalWrite(lightRelay,LOW);
lightStatus = true;
}
else if (lightValue > 2200) {
if (lightStatus) SerialBT.print("Bulb off?");
digitalWrite(lightRelay,HIGH);
lightStatus = false;
}
}
vTaskDelay(200 / portTICK_PERIOD_MS);
}
}
void smokeDetect(void *parameter) {
int smokeValue;
while (true) {
smokeValue = analogRead(smokeSensor);
Serial.print("Smoke: "); Serial.println(smokeValue);if (smokeValue >= 3200) {
if (!smokeStatus) SerialBT.print("Smoke active?");
digitalWrite(smokeLed, HIGH);
digitalWrite(smokeBuzzer, HIGH);
smokeStatus = true;
}
else if (smokeValue < 3200) {
if (smokeStatus) SerialBT.print("Smoke inactive?");
digitalWrite(smokeLed, LOW);
digitalWrite(smokeBuzzer, LOW);
smokeStatus = false;
}
vTaskDelay(1000 / portTICK_PERIOD_MS);
}
}
/**
* REPAIRED TASK: Now sets the global override flag instead of using
suspend/resume.
*/
void buttonDetect(void *parameter) {
bool lastButtonState = false;
const TickType_t OVERRIDE_TIME_MS = 5000; // 5 seconds override
while (true) {
bool currentButtonState = digitalRead(manualButton);
if (currentButtonState == HIGH && lastButtonState == LOW) {Serial.println("Manual button pressed! Toggling light.");
// Toggle light relay (Active LOW)
if (lightStatus) {
digitalWrite(lightRelay, HIGH); // Turn OFF
lightStatus = false;
SerialBT.print("Bulb off?");
} else {
digitalWrite(lightRelay, LOW); // Turn ON
lightStatus = true;
SerialBT.print("Bulb on?");
}
// Set the manual override timeout for 5 seconds
lightManualOverrideUntil = xTaskGetTickCount() +
(OVERRIDE_TIME_MS / portTICK_PERIOD_MS);
Serial.printf("Manual override set until Ticks: %lu\n",
lightManualOverrideUntil);
}
lastButtonState = currentButtonState;
vTaskDelay(50 / portTICK_PERIOD_MS); // Debounce delay
}
}
void ultrasonicDetect() {int distance;
long duration;
digitalWrite(trigger, LOW);
delayMicroseconds(2);
digitalWrite(trigger, HIGH);
delayMicroseconds(10);
digitalWrite(trigger, LOW);
duration = pulseIn(echo, HIGH);
distance = (duration / 2) * 0.0343;
Serial.print("Distance: ");
Serial.println(distance);
if (distance > 20 || distance == 0) {
if (ultrasonicStatus) SerialBT.print("Ultrasonic inactive?");
digitalWrite(ultrasonicLed, LOW);
ultrasonicStatus = false;
}
else if (distance <= 20) {
if (!ultrasonicStatus) SerialBT.print("Ultrasonic active?");
digitalWrite(ultrasonicLed, HIGH);
ultrasonicStatus = true;
}
}/**
* REPAIRED TASK: Now sets the global override flag instead of using
suspend/resume.
*/
void switchControl(void *parameter) {
char input;
const TickType_t BLUETOOTH_OVERRIDE_TIME_MS = 5000; // 5
seconds override
while (true) {
if (SerialBT.available() > 0) {
input = SerialBT.read();
// Global light manual override flag logic
bool lightCommandIssued = false;
switch (input) {
// M: Manual Mode for Fan and AutoLight OFF
case 'M':
vTaskSuspend(autoFan_handle);
lightManualOverrideUntil = xTaskGetTickCount() +
(BLUETOOTH_OVERRIDE_TIME_MS / portTICK_PERIOD_MS);
SerialBT.print("Manual Mode Active?");
break;
// F/Y: Fan Control
case 'F': digitalWrite(fanRelay, LOW); fanStatus = true;
SerialBT.print("Fan on?"); break;
case 'Y': digitalWrite(fanRelay, HIGH); fanStatus = false;
SerialBT.print("Fan off?"); break;// L/Z: Light Control - Sets the light status and the override flag
case 'L':
digitalWrite(lightRelay, LOW); lightStatus = true;
SerialBT.print("Bulb on?");
lightCommandIssued = true;
break;
case 'Z':
digitalWrite(lightRelay, HIGH); lightStatus = false;
SerialBT.print("Bulb off?");
lightCommandIssued = true;
break;
// A: Auto Mode (Resume Fan and clear light override)
case 'A':
vTaskResume(autoFan_handle);
lightManualOverrideUntil = 0; // Clear light override immediately
SerialBT.print("Auto Mode Active?");
break;
// O: All OFF (Manual override)
case 'O':
vTaskSuspend(autoFan_handle);
lightManualOverrideUntil = xTaskGetTickCount() +
(BLUETOOTH_OVERRIDE_TIME_MS / portTICK_PERIOD_MS);
SerialBT.print("All Off?");
digitalWrite(fanRelay, HIGH); digitalWrite(lightRelay, HIGH);
fanStatus = false; lightStatus = false;
break;}
// Set the override timeout for 5 seconds if a light command was issued
if (lightCommandIssued) {
lightManualOverrideUntil = xTaskGetTickCount() +
(BLUETOOTH_OVERRIDE_TIME_MS / portTICK_PERIOD_MS);
Serial.printf("Bluetooth Light override set until Ticks: %lu\n",
lightManualOverrideUntil);
}
}
vTaskDelay(100 / portTICK_PERIOD_MS);
}
}
//
======================================================
===========================================
// OLED DISPLAY TASK
//
======================================================
===========================================
void indicatorDisplay(void *parameter) {
int tempValue = 0;
int lightValue = 0;
char displayBuffer[32];
while (true) {
// REPAIR: Added robust queue peek checking.if (xQueuePeek(tempReading, (void*)&tempValue, 0) != pdPASS) {
tempValue = 0;
}
if (xQueuePeek(lightReading, (void*)&lightValue, 0) != pdPASS) {
lightValue = 0;
}
u8g2.clearBuffer();
// Title/Header
u8g2.setFont(u8g2_font_ncenB10_tr);
u8g2.drawStr(1, 12, "Monitor:");
// --- Temperature Display ---
u8g2.setFont(u8g2_font_ncenB14_tr);
sprintf(displayBuffer, "Temp: %d%sC", tempValue, "\xb0");
u8g2.drawStr(5, 35, displayBuffer);
// --- Light Intensity Display ---
u8g2.setFont(u8g2_font_ncenB10_tr);
sprintf(displayBuffer, "Light: %d ADC", lightValue);
u8g2.drawStr(5, 55, displayBuffer);
// Simple light status indicator
u8g2.setFont(u8g2_font_profont12_mf);
if(lightStatus) {
u8g2.drawStr(100, 55, "(ON)");} else {
u8g2.drawStr(100, 55, "(OFF)");
}
// Display Auto/Manual status
u8g2.setFont(u8g2_font_profont12_mf);
if (lightManualOverrideUntil == 0 && autoLightModeActive) {
u8g2.drawStr(80, 12, "AUTO");
} else {
u8g2.drawStr(80, 12, "MANUAL");
}
u8g2.sendBuffer();
vTaskDelay(500 / portTICK_PERIOD_MS);
}
}
//
======================================================
===========================================
// INTRO DISPLAY
//
======================================================
===========================================
void introDisplay() {
u8g2.clearBuffer();
u8g2.setFont(u8g2_font_ncenB14_tr);u8g2.drawStr(4, 25, "Smart Home");
u8g2.setFont(u8g2_font_ncenB10_tr);
u8g2.drawStr(18, 50, "System Ready");
u8g2.sendBuffer();
delay(4000);
}