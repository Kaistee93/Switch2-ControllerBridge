# ESP32-S3 Nintendo Switch Web-Controller Relay

Play on your Nintendo Switch or Switch 2 using practically any controller (like a PlayStation 4 gamepad) connected to your PC or Laptop over WiFi! 

This project turns an **ESP32-S3** into a wireless bridge. The ESP32 acts as a wired USB "HORIPAD" when plugged into your Switch. Meanwhile, it hosts a local web server. By opening this webpage on your Laptop/PC, the browser captures your PC gamepad inputs and streams them directly to the Switch with extremely low latency via WebSockets.

![WebUI Preview](https://raw.githubusercontent.com/Kaistee93/Switch2-ControllerBridge/main/WebUI.png)

## 📝 Important Notes & Requirements
*   **Browser Compatibility:** You must use a modern browser that supports the HTML5 Gamepad API (Google Chrome or Microsoft Edge are highly recommended).
*   **Controller Activation:** The browser will not detect your controller until you press at least one physical button *after* loading the page (this is a standard browser security feature).
*   **Correct USB Port:** Many ESP32-S3 development boards have two USB-C ports (one labeled "UART" / "COM" and one labeled "USB"). To connect the ESP32 to the Nintendo Switch, you **must** use the native "USB" port.
*   **Switch 2 Compatibility:** Nintendo has tightened USB accessory security on newer firmwares/hardware. The software is configured to spoof a licensed HORIPAD, but success may vary depending on future Switch firmware updates.

## 🛠️ Hardware Tested
*   **Board:** Freenove ESP32-S3 WROOM (ESP32 S3 Board Lite, Dual-core 32-bit 240 MHz)
*   **Controller:** PlayStation 4 (DualShock 4) connected via Bluetooth to a PC
*   **OS:** Windows 11 Laptop 

---

## 🚀 Flashing the Pre-compiled Binary (Easy Way)
If you don't want to mess with code or the Arduino IDE, you can directly flash the provided `.bin` file using a web browser!

1. Connect your ESP32-S3 to your PC.
2. Open the [Adafruit WebSerial ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) (Works best in Chrome/Edge).
3. Click **Connect** and select the COM port of your ESP32.
4. Set the **Flash Address (Offset)** to `0x10000`.
5. Click **Choose a file...** and select the `.bin` file from this repository.
6. Click **Program** and wait for the process to finish.

**First Boot / Network Setup:**
Once flashed, power up the ESP32. Use your phone or PC to search for a WiFi network called **`ESP32-Switch-Setup`**. Connect to it, and a captive portal will appear. Enter your home WiFi network credentials. The ESP32 will reboot and connect to your home network.

---

## 💻 Compiling it yourself (Arduino IDE)

### Prerequisites (Libraries)
Make sure you install the following libraries via the Arduino IDE Library Manager:
*   **WebSockets** by Markus Sattler (Tested with v2.7.2)
*   **WiFiManager** by tzapu (Tested with v2.0.17+)
*   **ESP32 Core** by Espressif (Tested with v3.3.11 / 3.x)

### ⚠️ Critical Arduino IDE Settings
The Nintendo Switch 2 will reject the controller if it presents itself as a Serial (CDC) device alongside the gamepad. You **must** configure your IDE exactly as shown below before compiling. 

For a visual reference, check the repository file `photo_5188168018394028069_y.jpg`.

**Tools Menu Configuration (Arduino IDE 2.3.x):**
*   **Board:** `ESP32S3 Dev Module`
*   **USB CDC On Boot:** `Disabled` *(CRITICAL for Switch 2!)*
*   **CPU Frequency:** `240MHz (WiFi)`
*   **Core Debug Level:** `None`
*   **USB DFU On Boot:** `Disabled`
*   **Erase All Flash Before Sketch Upload:** `Disabled`
*   **Events Run On:** `Core 1`
*   **Flash Mode:** `QIO 80MHz`
*   **Flash Size:** `8MB (64Mb)` *(Adjust if your board has different storage)*
*   **JTAG Adapter:** `Disabled`
*   **Arduino Runs On:** `Core 1`
*   **USB Firmware MSC On Boot:** `Disabled`
*   **Partition Scheme:** `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)`
*   **PSRAM:** `OPI PSRAM` *(Required for many Freenove boards)*
*   **Upload Mode:** `UARTO / Hardware CDC`
*   **Upload Speed:** `921600`
*   **USB Mode:** `USB-OTG (TinyUSB)` *(CRITICAL for HID emulation!)*
*   **Zigbee Mode:** `Disabled`

### Building
1. Clone this repository.
2. Open the sketch in Arduino IDE.
3. Apply the Tools menu settings above.
4. Hit Upload!

---

## 📄 License & Usage
This project is open-source. You are absolutely free to use, modify, fork, and distribute this software however you like. Contributions and pull requests are always welcome! Let's build something cool for the community.

---
*Note: Made with the help of AI*
