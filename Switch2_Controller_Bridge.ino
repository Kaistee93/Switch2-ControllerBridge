/*
  ============================================================================
  ESP32-S3 -> Nintendo Switch / Switch 2 Controller Relay via WiFi
  ============================================================================
  
  FEATURES:
  - Acts as a "HORIPAD for Nintendo Switch" (VID 0x0F0D / PID 0x00C1) via native 
    USB. This is accepted by Switch 1 and often Switch 2.
  - Uses WiFiManager: Opens an AP "ESP32-Switch-Setup" on first boot to 
    configure your local WiFi credentials without hardcoding them.
  - Hosts a Web UI to capture gamepad inputs (e.g., PS4 controller) from a 
    connected PC/Laptop and streams them via WebSockets to the Switch.

  ============================================================================
  ARDUINO IDE - BOARD SETTINGS (CRITICAL FOR SWITCH 2!)
  ============================================================================
  - Board:             "ESP32S3 Dev Module"
  - USB Mode:          "USB-OTG (TinyUSB)" 
  - USB CDC On Boot:   "Disabled" <-- CRITICAL! The Switch 2 rejects the device
                                      if it presents itself as a Serial Port (CDC)
                                      alongside the controller.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include "USB.h"
#include "USBHID.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
const uint16_t REPORT_INTERVAL_MS = 8;   // ~125 Hz polling rate
const uint16_t HEARTBEAT_INTERVAL_MS = 1000;

// ---------------------------------------------------------------------------
// USB-HID: HORIPAD for Nintendo Switch
// ---------------------------------------------------------------------------
static const uint8_t hid_report_descriptor[] = {
  0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
  0x09, 0x05,        // USAGE (Game Pad)
  0xA1, 0x01,        // COLLECTION (Application)
  0x15, 0x00,        //   LOGICAL_MINIMUM (0)
  0x25, 0x01,        //   LOGICAL_MAXIMUM (1)
  0x35, 0x00,        //   PHYSICAL_MINIMUM (0)
  0x45, 0x01,        //   PHYSICAL_MAXIMUM (1)
  0x75, 0x01,        //   REPORT_SIZE (1)
  0x95, 0x10,        //   REPORT_COUNT (16)
  0x05, 0x09,        //   USAGE_PAGE (Button)
  0x19, 0x01,        //   USAGE_MINIMUM (1)
  0x29, 0x10,        //   USAGE_MAXIMUM (16)
  0x81, 0x02,        //   INPUT (Data,Var,Abs)
  0x05, 0x01,        //   USAGE_PAGE (Generic Desktop)
  0x25, 0x07,        //   LOGICAL_MAXIMUM (7)
  0x46, 0x3B, 0x01,  //   PHYSICAL_MAXIMUM (315)
  0x75, 0x04,        //   REPORT_SIZE (4)
  0x95, 0x01,        //   REPORT_COUNT (1)
  0x65, 0x14,        //   UNIT (20)
  0x09, 0x39,        //   USAGE (Hat Switch)
  0x81, 0x42,        //   INPUT (Data,Var,Abs,Null)
  0x65, 0x00,        //   UNIT (0)
  0x95, 0x01,        //   REPORT_COUNT (1)
  0x81, 0x01,        //   INPUT (Cnst,Arr,Abs)
  0x26, 0xFF, 0x00,  //   LOGICAL_MAXIMUM (255)
  0x46, 0xFF, 0x00,  //   PHYSICAL_MAXIMUM (255)
  0x09, 0x30,        //   USAGE (X)   -> Left Stick X
  0x09, 0x31,        //   USAGE (Y)   -> Left Stick Y
  0x09, 0x32,        //   USAGE (Z)   -> Right Stick X
  0x09, 0x35,        //   USAGE (Rz)  -> Right Stick Y
  0x75, 0x08,        //   REPORT_SIZE (8)
  0x95, 0x04,        //   REPORT_COUNT (4)
  0x81, 0x02,        //   INPUT (Data,Var,Abs)
  0x06, 0x00, 0xFF,  //   USAGE_PAGE (Vendor Defined 0xFF00)
  0x09, 0x20,        //   USAGE (32)
  0x95, 0x01,        //   REPORT_COUNT (1)
  0x81, 0x02,        //   INPUT (Data,Var,Abs)
  0x0A, 0x21, 0x26,  //   USAGE (9761)
  0x95, 0x08,        //   REPORT_COUNT (8)
  0x91, 0x02,        //   OUTPUT (Data,Var,Abs)   (Rumble/LED)
  0xC0               // END_COLLECTION
};

enum SwitchButton : uint16_t {
  SW_Y       = 0x0001,
  SW_B       = 0x0002,
  SW_A       = 0x0004,
  SW_X       = 0x0008,
  SW_L       = 0x0010,
  SW_R       = 0x0020,
  SW_ZL      = 0x0040,
  SW_ZR      = 0x0080,
  SW_MINUS   = 0x0100,
  SW_PLUS    = 0x0200,
  SW_LCLICK  = 0x0400,
  SW_RCLICK  = 0x0800,
  SW_HOME    = 0x1000,
  SW_CAPTURE = 0x2000,
};

USBHID HID;

class SwitchControllerHID : public USBHIDDevice {
public:
  SwitchControllerHID(void) {
    static bool initialized = false;
    if (!initialized) {
      initialized = true;
      HID.addDevice(this, sizeof(hid_report_descriptor));
    }
  }
  void begin(void) { HID.begin(); }
  uint16_t _onGetDescriptor(uint8_t *buffer) {
    memcpy(buffer, hid_report_descriptor, sizeof(hid_report_descriptor));
    return sizeof(hid_report_descriptor);
  }
  bool sendReport(const uint8_t *data, size_t len) {
    return HID.SendReport(0, data, len);
  }
};

SwitchControllerHID SwitchPad;

// ---------------------------------------------------------------------------
// State Variables
// ---------------------------------------------------------------------------
uint8_t currentReport[8] = {0, 0, 8, 128, 128, 128, 128, 0};
const uint8_t neutralReport[8] = {0, 0, 8, 128, 128, 128, 128, 0};

volatile bool running = false;
int connectedClients = 0;
unsigned long lastReportTime = 0;
unsigned long lastHeartbeat = 0;

WebServer server(80);
WebSocketsServer webSocket(81);

// ---------------------------------------------------------------------------
// HTML Web Interface
// ---------------------------------------------------------------------------
const char INDEX_HTML[] = R"HTMLCONTENT(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Switch Controller</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 16px;
    background: #11141a; color: #e5e7eb;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  }
  h1 { font-size: 18px; margin: 0 0 4px 0; }
  .sub { color: #8b93a5; font-size: 13px; margin-bottom: 16px; }
  .card {
    background: #1b1f27; border: 1px solid #2a2f3a; border-radius: 12px;
    padding: 14px 16px; margin-bottom: 12px;
  }
  .status-row {
    display: flex; align-items: center; gap: 8px;
    padding: 5px 0; font-size: 14px;
  }
  .dot {
    width: 10px; height: 10px; border-radius: 50%;
    background: #f87171; flex-shrink: 0;
    transition: background 0.2s;
  }
  .btn {
    width: 100%; padding: 14px; font-size: 17px; font-weight: 600;
    border: none; border-radius: 10px; cursor: pointer;
    color: #0a0a0a; transition: opacity 0.15s;
  }
  .btn:active { opacity: 0.8; }
  .btn.start { background: #4ade80; }
  .btn.stop  { background: #f87171; }
  canvas { display: block; margin: 8px auto 0 auto; background: #0d0f14; border-radius: 8px; }
  .hint { font-size: 12px; color: #6b7280; margin-top: 8px; line-height: 1.5; }
  .small { font-size: 12px; color: #8b93a5; }
</style>
</head>
<body>
  <h1>ESP32-S3 &rarr; Switch Controller</h1>
  <div class="sub">Relay PC Gamepad Inputs (e.g. PS4 Controller)</div>
  <div class="card">
    <div class="status-row"><span class="dot" id="wsDot"></span><span id="wsText">Connecting...</span></div>
    <div class="status-row"><span class="dot" id="gpDot"></span><span id="gpText">No controller detected</span></div>
    <div class="status-row small">Connected Browser Clients: <span id="clients">-</span></div>
  </div>
  <div class="card">
    <button id="startStopBtn" class="btn start">&#9654; Start</button>
    <div class="status-row small" id="runText" style="margin-top:8px;">Stopped (Inputs are NOT being sent)</div>
  </div>
  <div class="card">
    <canvas id="padCanvas" width="340" height="230"></canvas>
    <div class="hint">
      Instructions: Connect a PS4 controller to your laptop (USB/Bluetooth) &middot;
      Press any button once to register it in the browser &middot;
      Then tap Start above.
    </div>
  </div>
<script>
const SW = {
  Y:0x0001, B:0x0002, A:0x0004, X:0x0008,
  L:0x0010, R:0x0020, ZL:0x0040, ZR:0x0080,
  MINUS:0x0100, PLUS:0x0200, LCLICK:0x0400, RCLICK:0x0800,
  HOME:0x1000, CAPTURE:0x2000
};
let ws = null;
let wsConnected = false;
let running = false;
let gpConnected = false;
let lastSentTime = 0;
const SEND_INTERVAL = 15;
function connectWS() {
  const url = "ws://" + location.hostname + ":81/";
  ws = new WebSocket(url);
  ws.onopen = () => { wsConnected = true; updateStatus(); };
  ws.onclose = () => { wsConnected = false; updateStatus(); setTimeout(connectWS, 1500); };
  ws.onerror = () => { try { ws.close(); } catch(e) {} };
  ws.onmessage = (evt) => {
    const parts = evt.data.split(",");
    if (parts[0] === "H") {
      document.getElementById("clients").textContent = parts[1];
    }
  };
}
connectWS();
function setDot(id, ok) {
  document.getElementById(id).style.background = ok ? "#4ade80" : "#f87171";
}
function updateStatus() {
  setDot("wsDot", wsConnected);
  document.getElementById("wsText").textContent = wsConnected ? "Connected to ESP32" : "Disconnected - retrying...";
  const btn = document.getElementById("startStopBtn");
  btn.textContent = running ? "\u23F9 Stop" : "\u25B6 Start";
  btn.className = "btn " + (running ? "stop" : "start");
  document.getElementById("runText").textContent = running
    ? "Active: Sending inputs to Nintendo Switch"
    : "Stopped (Inputs are NOT being sent)";
}
function updateGamepadStatus(ok, name) {
  if (ok === gpConnected) return;
  gpConnected = ok;
  setDot("gpDot", ok);
  document.getElementById("gpText").textContent = ok
    ? ("Controller found: " + (name || ""))
    : "No controller - press a button to register";
}
function setRunning(state) {
  running = state;
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send("R," + (running ? 1 : 0));
  }
  updateStatus();
}
document.getElementById("startStopBtn").addEventListener("click", () => {
  setRunning(!running);
});
function clamp255(v) { return Math.max(0, Math.min(255, Math.round(v))); }
function readGamepadState() {
  const pads = navigator.getGamepads();
  let gp = null;
  for (const p of pads) { if (p) { gp = p; break; } }
  if (!gp) return null;
  const b = gp.buttons;
  const pressed = i => b[i] && b[i].pressed;
  let buttons = 0;
  if (pressed(0)) buttons |= SW.B; 
  if (pressed(1)) buttons |= SW.A; 
  if (pressed(2)) buttons |= SW.Y; 
  if (pressed(3)) buttons |= SW.X; 
  if (pressed(4)) buttons |= SW.L;
  if (pressed(5)) buttons |= SW.R;
  if (pressed(6)) buttons |= SW.ZL;
  if (pressed(7)) buttons |= SW.ZR;
  if (pressed(8)) buttons |= SW.MINUS;  
  if (pressed(9)) buttons |= SW.PLUS;   
  if (pressed(10)) buttons |= SW.LCLICK;
  if (pressed(11)) buttons |= SW.RCLICK;
  if (pressed(16)) buttons |= SW.HOME;  
  const up = pressed(12), down = pressed(13), left = pressed(14), right = pressed(15);
  let hat = 8;
  if (up && right) hat = 1;
  else if (down && right) hat = 3;
  else if (down && left) hat = 5;
  else if (up && left) hat = 7;
  else if (up) hat = 0;
  else if (right) hat = 2;
  else if (down) hat = 4;
  else if (left) hat = 6;
  const dz = v => Math.abs(v) < 0.08 ? 0 : v;
  const toByte = v => clamp255((dz(v) + 1) * 127.5);
  const lx = toByte(gp.axes[0] || 0);
  const ly = toByte(gp.axes[1] || 0);
  const rx = toByte(gp.axes[2] || 0);
  const ry = toByte(gp.axes[3] || 0);
  return { buttons, hat, lx, ly, rx, ry, name: gp.id };
}
const canvas = document.getElementById("padCanvas");
const ctx = canvas.getContext("2d");
function drawButton(x, y, r, label, pressed) {
  ctx.beginPath();
  ctx.arc(x, y, r, 0, Math.PI * 2);
  ctx.fillStyle = pressed ? "#4ade80" : "#2a2f3a";
  ctx.fill();
  ctx.strokeStyle = "#444"; ctx.stroke();
  ctx.fillStyle = pressed ? "#0a0a0a" : "#9ca3af";
  ctx.font = "12px sans-serif";
  ctx.textAlign = "center"; ctx.textBaseline = "middle";
  ctx.fillText(label, x, y);
}
function drawStick(cx, cy, r, dx, dy, pressed, label) {
  ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.fillStyle = "#15181f"; ctx.fill();
  ctx.strokeStyle = "#333"; ctx.stroke();
  const nx = cx + dx * (r - 9);
  const ny = cy + dy * (r - 9);
  ctx.beginPath(); ctx.arc(nx, ny, 9, 0, Math.PI * 2);
  ctx.fillStyle = pressed ? "#4ade80" : "#818cf8";
  ctx.fill();
  ctx.fillStyle = "#6b7280"; ctx.font = "10px sans-serif";
  ctx.textAlign = "center"; ctx.fillText(label, cx, cy + r + 13);
}
function drawController(state) {
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  const b = state ? state.buttons : 0;
  const hat = state ? state.hat : 8;
  const has = m => (b & m) !== 0;
  const cx1 = w * 0.72, cy1 = h * 0.42, br = 16, bd = 26;
  drawButton(cx1, cy1 - bd, br, "X", has(SW.X));
  drawButton(cx1, cy1 + bd, br, "B", has(SW.B));
  drawButton(cx1 - bd, cy1, br, "Y", has(SW.Y));
  drawButton(cx1 + bd, cy1, br, "A", has(SW.A));
  const cx2 = w * 0.28, cy2 = h * 0.42;
  drawButton(cx2, cy2 - bd, br, "\u25B2", hat === 0 || hat === 1 || hat === 7);
  drawButton(cx2, cy2 + bd, br, "\u25BC", hat === 3 || hat === 4 || hat === 5);
  drawButton(cx2 - bd, cy2, br, "\u25C0", hat === 5 || hat === 6 || hat === 7);
  drawButton(cx2 + bd, cy2, br, "\u25B6", hat === 1 || hat === 2 || hat === 3);
  drawButton(w * 0.28, h * 0.14, 15, "L", has(SW.L));
  drawButton(w * 0.72, h * 0.14, 15, "R", has(SW.R));
  drawButton(w * 0.28, h * 0.05, 12, "ZL", has(SW.ZL));
  drawButton(w * 0.72, h * 0.05, 12, "ZR", has(SW.ZR));
  drawButton(w * 0.42, h * 0.28, 9, "-", has(SW.MINUS));
  drawButton(w * 0.58, h * 0.28, 9, "+", has(SW.PLUS));
  drawButton(w * 0.5, h * 0.14, 11, "H", has(SW.HOME));
  const lx = state ? (state.lx - 128) / 128 : 0;
  const ly = state ? (state.ly - 128) / 128 : 0;
  const rx = state ? (state.rx - 128) / 128 : 0;
  const ry = state ? (state.ry - 128) / 128 : 0;
  drawStick(w * 0.28, h * 0.72, 32, lx, ly, has(SW.LCLICK), "L-Stick");
  drawStick(w * 0.72, h * 0.72, 32, rx, ry, has(SW.RCLICK), "R-Stick");
}
function loop() {
  const state = readGamepadState();
  updateGamepadStatus(!!state, state ? state.name : "");
  drawController(state);
  if (state && running && ws && ws.readyState === WebSocket.OPEN) {
    const now = performance.now();
    if (now - lastSentTime >= SEND_INTERVAL) {
      lastSentTime = now;
      ws.send("S," + state.buttons + "," + state.hat + "," + state.lx + "," + state.ly + "," + state.rx + "," + state.ry);
    }
  }
  requestAnimationFrame(loop);
}
updateStatus();
drawController(null);
requestAnimationFrame(loop);
</script>
</body>
</html>
)HTMLCONTENT";

// ---------------------------------------------------------------------------
// Server / WebSocket Handlers
// ---------------------------------------------------------------------------
void handleRoot() { server.send(200, "text/html", INDEX_HTML); }
void handleNotFound() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }
void applyNeutral() { memcpy(currentReport, neutralReport, 8); }

void handleWsMessage(uint8_t *payload, size_t length) {
  char buf[80];
  size_t len = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
  memcpy(buf, payload, len);
  buf[len] = 0;

  if (buf[0] == 'R') {
    int val = 0;
    sscanf(buf, "R,%d", &val);
    running = (val != 0);
    if (!running) applyNeutral();
    String ack = running ? "ACK,RUN,1" : "ACK,RUN,0";
    webSocket.broadcastTXT(ack);
    return;
  }

  if (buf[0] == 'S') {
    int buttons = 0, hat = 8, lx = 128, ly = 128, rx = 128, ry = 128;
    int n = sscanf(buf, "S,%d,%d,%d,%d,%d,%d", &buttons, &hat, &lx, &ly, &rx, &ry);
    if (n == 6 && running) {
      currentReport[0] = buttons & 0xFF;
      currentReport[1] = (buttons >> 8) & 0xFF;
      currentReport[2] = (uint8_t)constrain(hat, 0, 8);
      currentReport[3] = (uint8_t)constrain(lx, 0, 255);
      currentReport[4] = (uint8_t)constrain(ly, 0, 255);
      currentReport[5] = (uint8_t)constrain(rx, 0, 255);
      currentReport[6] = (uint8_t)constrain(ry, 0, 255);
      currentReport[7] = 0;
    }
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      connectedClients = max(0, connectedClients - 1);
      running = false;
      applyNeutral();
      break;
    case WStype_CONNECTED:
      connectedClients++;
      break;
    case WStype_TEXT:
      handleWsMessage(payload, length);
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100); // Delay verkürzt, damit USB schneller reagiert

  // --- 1. USB HID ZUERST INITIALISIEREN! ---
  // Das muss sofort passieren, sonst gibt es bei der Switch einen Timeout
  USB.VID(0x0F0D);
  USB.PID(0x00C1);
  USB.manufacturerName("HORI CO.,LTD.");
  USB.productName("HORIPAD S");
  SwitchPad.begin();
  USB.begin();

  // --- 2. DANN ERST WLAN (WiFiManager) ---
  WiFiManager wifiManager;
  // wifiManager.resetSettings(); // Auskommentieren, um WLAN-Daten zu löschen
  wifiManager.autoConnect("ESP32-Switch-Setup");

  // --- 3. SERVER STARTEN ---
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  applyNeutral();
}

void loop() {
  server.handleClient();
  webSocket.loop();

  unsigned long now = millis();

  if (now - lastReportTime >= REPORT_INTERVAL_MS) {
    lastReportTime = now;
    SwitchPad.sendReport(running ? currentReport : neutralReport, 8);
  }

  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeat = now;
    String status = "H," + String(connectedClients);
    webSocket.broadcastTXT(status);
  }
}
