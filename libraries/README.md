# Libraries

This project is developed using the Arduino framework for the ESP32 and requires the following libraries.

## Required Libraries

| Library | Purpose |
|---------|---------|
| ESP32 Arduino Core | ESP32 development framework |
| BluetoothSerial | Bluetooth Serial (SPP) communication |
| DHT Sensor Library | Temperature and humidity sensing |
| Adafruit Unified Sensor | Dependency for DHT library |
| U8g2 | OLED graphics library |
| Wire | I2C communication |
| Ticker | Periodic task scheduling |
| FreeRTOS | Real-time multitasking (included with ESP32 Arduino Core) |

---

## Install from Arduino IDE

Open:

**Arduino IDE → Sketch → Include Library → Manage Libraries**

Install the following libraries:

- DHT sensor library by Adafruit
- Adafruit Unified Sensor
- U8g2 by oliver
- Ticker

The following are included automatically with the ESP32 Arduino Core:

- BluetoothSerial
- Wire
- FreeRTOS

---

## ESP32 Board Package

Install the **ESP32 by Espressif Systems** board package using the Arduino Boards Manager.

Board Used:

- ESP32 DevKitC

---

## Tested Environment

- Arduino IDE 2.x
- ESP32 Arduino Core
- FreeRTOS
- ESP32 DevKitC

---

## Notes

After installing all required libraries and the ESP32 board package, open the project `.ino` file, select the correct COM port and ESP32 board, then compile and upload the program.
