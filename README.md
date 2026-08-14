# ESP32-S3 Nintendo Switch Web-Controller Relay

Play on your Nintendo Switch or Switch 2 using practically any controller (like a PlayStation 4 gamepad) connected to your PC or Laptop over WiFi! 

This project turns an **ESP32-S3** into a wireless bridge. The ESP32 acts as a wired USB "HORIPAD" when plugged into your Switch. Meanwhile, it hosts a local web server. By opening this webpage on your Laptop/PC, the browser captures your PS4 controller inputs and streams them directly to the Switch with extremely low latency via WebSockets.

## 🛠️ Hardware Tested
*   **Board:** Freenove ESP32-S3 WROOM (ESP32 S3 Board Lite, Dual-core 32-bit 240 MHz)
*   **Controller:** PlayStation 4 (DualShock 4) connected via Bluetooth to a PC
*   **OS:** Windows 11 Laptop (Browser: Chrome/Edge recommended for Gamepad API support)

## 🚀 How to use (For pre-compiled Binary users)
1. Flash the provided `.bin` file to your ESP32-S3 via any Web-Flasher.
2. Plug the ESP32-S3 into a power source (or directly into the Switch).
3. On your phone or laptop, search for a new WiFi network called **`ESP32-Switch-Setup`** and connect to it.
4. A captive portal will pop up automatically. Select your home WiFi network and enter your password.
5. Once connected, plug the ESP32-S3 into your Switch's USB port (in docked mode or via a Type-C adapter).
6. Find the IP address of the ESP32-S3 in your home router's interface. 
7. Open that IP address in your PC's browser, connect your PS4 controller, press any button to register it, and click **Start**!

## 💻 For Developers (Compile it yourself)

### Prerequisites (Libraries)
Make sure you install the following libraries via the Arduino IDE Library Manager:
*   **WebSockets** by Markus Sattler (Tested with v2.7.2)
*   **WiFiManager** by tzapu (Tested with v2.0.17+)
*   **ESP32 Core** by Espressif (Tested with v3.3.11 / 3.x)

### ⚠️ Critical Arduino IDE Settings (Tools Menu)
The Nintendo Switch 2 has strict USB security protocols. If the ESP32 presents itself as a Serial Device alongside the controller, the Switch will reject it. Ensure these settings are applied before compiling:

*   **Board:** `ESP32S3 Dev Module`
*   **USB Mode:** `USB-OTG (TinyUSB)` *(Required for HID capabilities)*
*   **USB CDC On Boot:** `Disabled` *(CRITICAL! Without this, Switch 2 will not recognize the controller!)*

### Building
1. Clone this repository.
2. Open the sketch in Arduino IDE.
3. Configure the Tools menu as described above.
4. Hit Upload!

## 🎮 Troubleshooting
*   **Switch 2 doesn't react:** Double-check that `USB CDC On Boot` is set to `Disabled` before compiling. Try completely restarting your Switch. Ensure you've plugged the USB cable into the native USB port of the ESP32-S3 (some dev boards have two ports: one for UART, one for USB).
*   **Controller not detected on Web UI:** Ensure you have pressed a physical button on your PS4 controller *after* opening the webpage. The HTML5 Gamepad API requires user interaction to prevent fingerprinting.
*   **To change WiFi networks:** Since `WiFiManager` saves your credentials, it will auto-connect next time. To reset the credentials, uncomment `wifiManager.resetSettings();` in the `setup()` function, flash once, then re-comment and flash again.
