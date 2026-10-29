/*
  LZ1AQ Loop Controller - WT32-ETH01 / WT32-ETH02
  Based on original work by Anthony DiIorio (VE2HEW)
  Adapted for WT32-ETH01/02 (ESP32 + LAN8720A Ethernet) by Joeri (PE5JW)

  Libraries required:
    - ESP32 Arduino core: https://github.com/espressif/arduino-esp32 (v2.x or higher)
    - ESPmDNS  (included in ESP32 core)
    - WebServer (included in ESP32 core)
    - ETH      (included in ESP32 core)

  Board selection in Arduino IDE:
    Tools > Board > ESP32 Arduino > WT32-ETH01 Ethernet Module
    (or select "ESP32 Dev Module" if WT32-ETH01 is not listed)

  WT32-ETH01 Ethernet wiring is handled internally via ETH.h / LAN8720A.
  No external Ethernet wiring needed beyond the RJ45 connector on the module.
*/

#include <ETH.h>
#include <ESPmDNS.h>
#include <WebServer.h>

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
const char *eth_hostname = "loopcontroller";

WebServer server(80);

char activeMode[10];
int windowHeight = 90;

// ---------------------------------------------------------------------------
// Auxiliary Relay #4 Options
// ---------------------------------------------------------------------------
bool auxEnable = true;        // Enable 4th relay in web UI
bool auxNC     = true;        // Visual indicator for Normally Closed
const char *auxLabel = "Power Supply";  // Label for auxiliary relay

// ---------------------------------------------------------------------------
// WT32-ETH01 GPIO pin assignments for relay board
//
// The WT32-ETH01 uses several GPIO pins internally for the LAN8720A PHY.
// Avoid: 0, 1, 2, 3, 5, 16, 17, 18, 19, 23, 25, 26, 27
// Safe for general use: IO32, IO33, IO34(input only), IO35(input only),
//                       IO36(input only), IO39(input only), IO4, IO14, IO15
//
// Using IO32, IO33, IO4, IO15 for the four relays (active LOW board):
// ---------------------------------------------------------------------------
#define Relay1  32   // Loop A control
#define Relay2  33   // Loop B control
#define Relay3   4   // Vertical control
#define Relay4  15   // Auxiliary relay (Power Supply etc.)

// ---------------------------------------------------------------------------
// Ethernet link state tracking
// ---------------------------------------------------------------------------
static bool eth_connected = false;

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH: started");
      ETH.setHostname(eth_hostname);
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH: cable connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH IP: ");
      Serial.println(ETH.localIP());
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH: disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH: stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// LZ1AQ relay switching
//
// Relays are Active LOW (relay board pulls IN_ to GND to energise).
// Relay1 is wired Normally Closed so the antenna defaults to Loop A
// with no power applied to MCU or relay board.
// NC wiring means Relay1 logic is inverted vs. the LZ1AQ table.
//
//  LZ1AQ truth table (1 = energised):
//    1 0 0  Loop A
//    0 1 0  Loop B
//    1 1 0  A + B (crossed)
//    0 0 1  Vertical
// ---------------------------------------------------------------------------

void loopA() {   // 1 0 0
  digitalWrite(Relay1, HIGH);  // NC-flipped: HIGH = relay NOT energised = NC closed = signal through
  digitalWrite(Relay2, HIGH);  // HIGH = not energised
  digitalWrite(Relay3, HIGH);  // HIGH = not energised
  strcpy(activeMode, "A");
}

void loopB() {   // 0 1 0
  digitalWrite(Relay1, LOW);   // NC-flipped: LOW = relay energised = NC opens
  digitalWrite(Relay2, LOW);   // LOW = energised
  digitalWrite(Relay3, HIGH);
  strcpy(activeMode, "B");
}

void crossed() { // 1 1 0
  digitalWrite(Relay1, HIGH);  // NC-flipped
  digitalWrite(Relay2, LOW);
  digitalWrite(Relay3, HIGH);
  strcpy(activeMode, "Crossed");
}

void vertical() { // 0 0 1
  digitalWrite(Relay1, LOW);   // NC-flipped
  digitalWrite(Relay2, HIGH);
  digitalWrite(Relay3, LOW);
  strcpy(activeMode, "Vertical");
}

void auxToggle() {
  digitalWrite(Relay4, !digitalRead(Relay4));
}

// ---------------------------------------------------------------------------
// HTML helpers
// ---------------------------------------------------------------------------
String activeButton(const char *checkMode, const char *path, const char *btnText) {
  String output = "<a class=\"btn ";
  if (strcmp(activeMode, checkMode) == 0) {
    output += "btn-success\" ";
  } else {
    output += "btn btn-dark\" ";
  }
  output += "href=\"/";
  output += path;
  output += "\">";
  output += btnText;
  output += "</a> ";
  return output;
}

String auxButton(const char *path, const char *btnText) {
  String output = "<a class=\"btn ";
  if (!digitalRead(Relay4)) {
    output += auxNC ? "btn-dark\" " : "btn-success\" ";
  } else {
    output += auxNC ? "btn-success\" " : "btn btn-dark\" ";
  }
  output += "href=\"/";
  output += path;
  output += "\">";
  output += btnText;
  output += "</a> ";
  return output;
}

// ---------------------------------------------------------------------------
// Web page
// ---------------------------------------------------------------------------
void handleRoot() {
  String html =
    "<!DOCTYPE html><html><head>"
    "<title>LZ1AQ Controller</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<link rel=\"icon\" type=\"image/png\" href=\"https://i.imgur.com/nanrKpL.png\">"
    "<link href=\"https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/css/bootstrap.min.css\""
    " rel=\"stylesheet\""
    " integrity=\"sha384-1BmE4kWBq78iYhFldvKuhfTAU6auU8tT94WrHftjDbrCEXSU1oBoqyl2QvZ6jIW3\""
    " crossorigin=\"anonymous\">"
    "<style>body{background:#000;color:#fff;}"
    "#main{padding-top:10px;margin:0 auto;width:310px;}"
    "</style></head><body><div id=\"main\">"
    "<h3>Loop Controller "
    "<a href=\"#\" onClick=\"Tiny=window.open('#','Tiny','width=330,height=";
  html += windowHeight;
  html +=
    "');return false;\">&#128377;</a></h3>";

  html += activeButton("A",       "a", "Loop A");
  html += activeButton("B",       "b", "Loop B");
  html += activeButton("Crossed", "x", "A + B");
  html += activeButton("Vertical","v", "Vertical");

  if (auxEnable) {
    html += "<div style=\"margin-top:5px\"></div>";
    html += auxButton("aux", auxLabel);
  }

  // Show IP address at the bottom
  html += "<p style=\"font-size:0.75em;margin-top:12px;color:#888;\">IP: ";
  html += ETH.localIP().toString();
  html += "</p>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleLoopA()    { loopA();    server.sendHeader("Location","/",true); server.send(303,"text/plain",""); }
void handleLoopB()    { loopB();    server.sendHeader("Location","/",true); server.send(303,"text/plain",""); }
void handleCrossed()  { crossed();  server.sendHeader("Location","/",true); server.send(303,"text/plain",""); }
void handleVertical() { vertical(); server.sendHeader("Location","/",true); server.send(303,"text/plain",""); }
void handleAux()      { auxToggle(); server.sendHeader("Location","/",true); server.send(303,"text/plain",""); }

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nLZ1AQ Loop Controller - WT32-ETH01");

  // Relay GPIO initialisation
  pinMode(Relay1, OUTPUT);
  pinMode(Relay2, OUTPUT);
  pinMode(Relay3, OUTPUT);
  pinMode(Relay4, OUTPUT);

  // Default state: Loop A (Relay1 NC, others de-energised)
  digitalWrite(Relay1, HIGH);
  digitalWrite(Relay2, HIGH);
  digitalWrite(Relay3, HIGH);
  digitalWrite(Relay4, HIGH);  // Aux relay OFF by default (change to LOW for ON)
  strcpy(activeMode, "A");

  if (auxEnable) {
    windowHeight = 135;
  }

  // Register Ethernet event handler BEFORE ETH.begin()
  WiFi.onEvent(WiFiEvent);

  // Start Ethernet (WT32-ETH01 defaults)
  // ETH.begin(phy_addr, power_pin, mdc_pin, mdio_pin, phy_type, clk_mode)
  ETH.begin(1, 16, 23, 18, ETH_PHY_LAN8720, ETH_CLOCK_GPIO0_IN);

  // Wait up to 10 s for a link + DHCP
  Serial.print("Waiting for Ethernet");
  unsigned long t = millis();
  while (!eth_connected && millis() - t < 10000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (!eth_connected) {
    Serial.println("No Ethernet link – continuing anyway (check cable/switch)");
  }

  // mDNS: reachable as http://loopcontroller.local/
  if (!MDNS.begin(eth_hostname)) {
    Serial.println("mDNS: failed to start");
  } else {
    Serial.println("mDNS: loopcontroller.local");
    MDNS.addService("http", "tcp", 80);
  }

  // Web server routes
  server.on("/",    handleRoot);
  server.on("/a",   handleLoopA);
  server.on("/b",   handleLoopB);
  server.on("/x",   handleCrossed);
  server.on("/v",   handleVertical);
  server.on("/aux", handleAux);

  server.begin();
  Serial.println("Web server started");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
  server.handleClient();
}
