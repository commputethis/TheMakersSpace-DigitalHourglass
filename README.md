# The Maker's Space Digital Hourglass - ESP32 Dual TFT Hourglass Clock

A digital hourglass clock built on an ESP32, featuring two circular TFT screens that display a sand animation synchronized to the current time. The project includes a full web configuration interface for setting the timer mode, NTP server, timezone, and a full suite of color options, with all settings stored persistently in EEPROM.

![Image of the final project](images/HourGlass.png)

---

## Table of Contents

1.  [Features](#features)
2.  [Hardware Required](#hardware-required)
3.  [Wiring Diagram](#wiring-diagram)
4.  [Software Setup](#software-setup)
5.  [Configuration](#configuration)
6.  [How It Works](#how-it-works)
7.  [Troubleshooting](#troubleshooting)
8.  [Attributions](#Attributions)

---

## Features

-   **Dual TFT Displays:** Uses two 240x240 circular GC9A01A TFT displays to show the upper and lower "bulbs" of the hourglass.
-   **Accurate Timekeeping:** Keeps time using a DS3231 Real-Time Clock (RTC) module, synchronized via NTP over WiFi.
-   **Two Timer Modes:**
    -   **Minute Mode:** The sand animation completes one full cycle every minute.
    -   **Hour Mode:** The sand animation completes one full cycle every hour.
-   **Web Configuration:** A built-in web server allows you to configure all settings without reprogramming the device:
    -   Toggle between Minute and Hour mode.
    -   Set the NTP server.
    -   Select your timezone from a dropdown list.
    -   Toggle between 12-hour and 24-hour time format.
    -   Customize the colors of the clock text, background, and sand.
-   **Persistent Settings:** All configuration is saved to the ESP32's EEPROM, so your settings are preserved after a power cycle.
-   **WiFiManager with Unique SSID:** For easy initial WiFi setup. The ESP32 creates a unique access point for each board (e.g., `Hourglass-A4CF12...`) to prevent confusion in multi-device environments like classrooms.

---

## Hardware Required

| Component | Quantity | Notes |
|---|---|---|
| [ESP32 DevKit V1](https://www.amazon.com/dp/B0C7C2HQ7P) | 1 | The "brains" of the project. |
| [1.28" GC9A01A Circular TFT (240x240)](https://www.amazon.com/Teyleten-Robot-Display-Interface-240x240/dp/B0B7TFRNN1) | 2 | For the upper and lower glass displays. |
| [DS3231 Real-Time Clock (RTC) Module](https://www.amazon.com/dp/B08X4H3NBR) | 1 | For accurate timekeeping. |
| [Female to Male Jumper Wires](https://www.amazon.com/dp/B07GD1R5MS) | 14 | For power, ground, SDA, and SCL connections. |
| [Female to Female Jumper Wires](https://www.amazon.com/dp/B07GD312VG) | 6 | For DC, CS, and RST connections. |
| [Male to Male Jumper Wires](https://www.amazon.com/dp/B07GD1ZCHQ) | 2 | For power and ground connection on RTC. |
| [221-413 Wago Lever Nuts](https://www.amazon.com/dp/B0957T1S9C) | 2 | For MOSI/SDA and SCK/SCL connections. |
| [221-415 Wago Lever Nuts](https://www.amazon.com/dp/B0957T1S9C) | 2 | For Power and Ground connections. |
| 5V Power Supply | 1 | Ensure it can provide enough current for both displays (at least 500mA is recommended). |
| [3D Printed Case](files/Digital_Hourglass_Remix.3mf) | 1 | Hourglass looking enclosure to house the components. |

---

## Wiring Diagram

The following table shows the correct pin assignments for the ESP32 DevKit V1. **Double-check your specific board's pinout diagram**, as "D" numbers can vary. The GPIO number is the most reliable reference. Note: pins below are for board used in this project's ESP32 DevKit V1.

| Device Pin | Connect to ESP32 Pin | Typical "D" Number | Notes |
|---|---|---|---|
| **SPI Bus (Shared)** | | | |
| TFT SDA (SPI MOSI) | `GPIO 23` | `D23` | Connect to 221-413 Wago Lever Nut using Female to Male Jumper Wire. |
| TFT SCL (SPI SCK) | `GPIO 18` | `D18` | Connect to 221-413 Wago Lever Nut using Female to Male Jumper Wire. |
| **TFT 1 (Upper Glass)** | | | |
| CS (Chip Select) | `GPIO 2` | `D2` | Using Female to Female Jumper Wire. |
| DC (Data/Command) | `GPIO 5` | `D5` | Using Female to Female Jumper Wire. |
| RST (Reset) | `GPIO 4` | `D4` | Using Female to Female Jumper Wire. |
| VCC | `3.3V` | `3V3` | Connect to 221-415 Wago Lever Nut using Female to Male Jumper Wire. |
| GND | `GND` | `GND` | Connect to 221-415 Wago Lever Nut using Female to Male Jumper Wire. |
| **TFT 2 (Lower Glass)** | | | |
| CS (Chip Select) | `GPIO 17` | `TX2` | Using Female to Female Jumper Wire. |
| DC (Data/Command) | `GPIO 19` | `D19` | Using Female to Female Jumper Wire. |
| RST (Reset) | `GPIO 15` | `D15` | Using Female to Female Jumper Wire. |
| VCC | `3.3V` | `3V3` | Connect to 221-415 Wago Lever Nut using Female to Male Jumper Wire. |
| GND | `GND` | `GND` | Connect to 221-415 Wago Lever Nut using Female to Male Jumper Wire. |
| **RTC Module (DS3231)** | | | |
| SDA (I2C Data) | `GPIO 21` | `D21` | Using Female to Male Jumper Wire. |
| SCL (I2C Clock) | `GPIO 22` | `D22` | Using Female to Male Jumper Wire. |
| VCC | `3.3V` | `3V3` | Connect to 221-415 Wago Lever Nut using Male to Male Jumper Wire. |
| GND | `GND` | `GND` | Connect to 221-415 Wago Lever Nut using Male to Male Jumper Wire. |

---

## Software Setup

### 1. Prerequisites

-   **Arduino IDE:** Ensure you have the latest version installed.
-   **ESP32 Board Manager:** In Arduino IDE, go to `File > Preferences`. Add this URL to "Additional Board Manager URLs":
    ```
    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
    ```
    Then, go to `Tools > Board > Boards Manager...`, search for "esp32", and install the package.

### 2. Install Required Libraries

Go to `Tools > Manage Libraries...` and install the following libraries:

-   `Adafruit GFX Library`
-   `Adafruit GC9A01A`
-   `RTClib by Adafruit`
-   `WiFiManager by tzapu`
-   `NTPClient by Fabrice Weinberg`
-   `ArduinoJson by Benoit Blanchon` (Required by WiFiManager)

### 3. Load the Code

The project is split into two files.

1.  **Create the Main Sketch (`.ino`):**
    -   Create a new sketch in the Arduino IDE.
    -   Copy the entire [HourGlass.ino](/files/HourGlass/HourGlass.ino) code and paste it into this sketch.
2.  **Create the Color Header File (`colors.h`):**
    -   In the Arduino IDE, click the down-arrow button on the far right of the tabs and select "New Tab".
    -   Name the new tab `colors.h`.
    -   Copy the entire content of your [colors.h](/files/HourGlass/colors.h) file and paste it into this new tab.
3.  **Upload:**
    -   Select your ESP32 board from `Tools > Board > ESP32 Arduino > ESP32 Dev Module`.
    -   Connect your ESP32 to your computer and select the correct COM port.
    -   Click "Upload".

---

## Configuration

### First-Time WiFi Setup

1.  Power on the device.
2.  The upper TFT will display "WiFi..." and the **unique SSID for that specific board** (e.g., `Hourglass-A4CF12C8D6E4`).
3.  On your phone or computer, scan for WiFi networks and connect to the SSID shown on the screen.
4.  A captive portal should open automatically. If not, navigate to `192.168.4.1`.
5.  Select your WiFi network and enter the password.
6.  The ESP32 will connect, and the hourglass will start.

### Web Configuration Interface

Once the ESP32 is connected to your network, you can access the configuration page at any time.

1.  Find the ESP32's IP address from the Arduino Serial Monitor.
2.  Open a web browser and navigate to that IP address (e.g., `http://192.168.1.123`).
3.  You will see the status page. Click the **Configure** button.
4.  On the configuration page, you can:
    -   **Timer Mode:** Switch between `Minute Mode` and `Hour Mode`.
    -   **Time Format:** Switch between 12-Hour Format and 24-Hour Format.
    -   **NTP Server:** Change the server used for time synchronization (e.g., `pool.ntp.org`).
    -   **Timezone:** Select your local timezone from the dropdown list.
    -   **Clock Color:** Choose the color for the time display on the upper screen.
    -   **Background Color:** Choose the background color for both screens.
    -   **Sand Color:** Choose the color of the animated sand.
5.  Click **Save & Restart**. The device will reboot with the new settings.

---

## How It Works

-   **Time Source:** The ESP32 first tries to get the time from an NTP server. If successful, it sets the DS3231 RTC. If NTP fails, it falls back to the time compiled into the sketch. The RTC ensures the device keeps accurate time even if WiFi is lost.
-   **Time Calculation:** The UTC time from the RTC is adjusted by the timezone offset (in minutes) that you set in the web interface to get the correct local time.
-   **Time Format:** The `drawTimeDisplay()` function checks the `configIs24Hour` setting. If true, it displays the hour in 24-hour format (e.g., 14:00). If false, it displays the hour in 12-hour format (e.g., 2:00).
-   **Unique ID:** The device generates a unique ID from its MAC address. This ID is used to create a unique WiFi SSID (`Hourglass-` + ID) for the initial setup portal, making it easy to identify individual devices in a classroom.
-   **Color Customization:** The `colors.h` file defines a palette of colors. The web interface lets you select an index from this palette for the clock, background, and sand. These choices are saved to EEPROM and applied at startup.
-   **Animation:** The `drawFrame()` function calculates how far through the current minute or hour the device is. It then draws the corresponding amount of sand in the upper and lower bulbs.
-   **Sand Stream:** The `drawSandStream()` function creates the illusion of flowing sand by drawing and erasing small circles in the center column. It is carefully designed to prevent "ghosting" or artifacts on the screen.
-   **Web Server:** A lightweight web server runs in the background, allowing you to change settings without interrupting the main animation loop.

---

## Troubleshooting

-   **Displays are blank or white:**
    -   Check all wiring, especially `3.3V`, `GND`, `CS`, and `DC`.
    -   Ensure your 5V power supply can provide enough current (at least 500mA is recommended).
    -   Make sure you have installed the `Adafruit_GC9A01A` library.
-   **WiFi doesn't connect:**
    -   On first boot, it will create a unique access point (e.g., `Hourglass-A4CF12...`). **Look at the upper TFT screen to see the exact SSID.** Connect to that to configure WiFi.
    -   Double-check your WiFi password in the portal.
-   **Time is incorrect:**
    -   Check the NTP server setting in the web config. `time.google.com` is a reliable default.
    -   Ensure you have selected the correct timezone. The offset is shown in minutes from UTC.
-   **Code won't compile:**
    -   Make sure you have installed all the required libraries listed in the Software Setup section.
    -   Ensure you have created the `colors.h` tab and pasted the color definitions into it. The name must match exactly.

---

## Attributions

- This was originally created by Markus Opitz and published on [Instructables](https://www.instructables.com/Digital-Hourglass/).
- The 3D case was redesigned by Don Potbury.
