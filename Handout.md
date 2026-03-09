# The Maker's Space - March 9th, 2026

### **Build a Digital Hourglass!**

**A modern take on a classic timepiece using an ESP32, dual TFT screens, and web-based configuration.**

---

#### **What You'll Build**

A functional hourglass that:
-   Shows a mesmerizing sand animation synced to the current time.
-   Runs in **Minute Mode** (1 min/cycle) or **Hour Mode** (1 hr/cycle).
-   Displays time in **12-hour** or **24-hour** format.
-   Connects to WiFi for automatic timekeeping.
-   Is fully customizable via a simple web interface on your phone or computer.

---

#### **Core Components**

| Component | Role |
|---|---|
| **ESP32 DevKit V1** | The "brain" that runs the code, animation, and web server. |
| **(2x) GC9A01A TFT Displays** | The "eyes" that show the upper and lower bulbs of the hourglass. |
| **DS3231 RTC Module** | The "heartbeat" that keeps perfect time, even without WiFi. |
| **Jumper Wires & Power** | Connects everything and provides power. |

---

#### **The Build Process (At a Glance)**

1.  **Wire It Up:** Connect the components to the ESP32 following the wiring diagram. Pay close attention to the `CS`, `DC`, and `RST` pins for each display.
2.  **Load the Code:** Install the required libraries in the Arduino IDE and upload the provided `HourGlass.ino` and `colors.h` files.
3.  **Configure WiFi:** On first boot, the ESP32 creates its own WiFi network (e.g., `Hourglass-A4CF12...`). Connect to it with your phone to set up your home WiFi.
4.  **Customize!** Once connected to your network, find the device's IP address in the Arduino Serial Monitor. Open it in a browser to access the configuration page. Here you can:
    -   Set your timezone and enable/disable Daylight Saving Time.
    -   Choose your preferred time format.
    -   Pick custom colors for the clock, background, and sand.
    -   Switch between Minute and Hour modes.

---

#### **Key Features to Explore**

-   **Web Configuration:** No reprogramming needed! Change all settings from a simple web page.
-   **45+ Timezones:** Select your location from a named list. The device handles the rest.
-   **Automatic DST:** When enabled, it automatically adjusts for Daylight Saving Time based on your timezone's rules.
-   **Factory Reset:** A built-in option to wipe all settings and start fresh.

---

#### **Need Help?**

All the details you need are in the project's GitHub repository, including:
-   Full wiring diagram
-   Complete source code
-   Troubleshooting guide
-   3D models for the case

**Visit:**

**`https://github.com/commputethis/TheMakersSpace-DigitalHourglass`**
