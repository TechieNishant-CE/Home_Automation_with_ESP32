/* =====================================================================
   HOME AUTOMATION SYSTEM  -  ESP32 30-Pin Board (Final Defense)
   =====================================================================
   Modes of Control:
     1) SENSOR CONTROL    -> DHT22 auto-turns Fan (Relay 3) ON/OFF based on threshold
     2) DASHBOARD CONTROL -> Pure HTML/CSS web dashboard (no JS, mobile-responsive)
     
   Feedback:
     - 1602 LCD via HW-061 I2C interface backpack (SDA=21, SCL=22)
       Displays Temp, Humidity, and Relay states in rotation.

   NOTE ON WI-FI CREDENTIALS:
     Never commit real Wi-Fi credentials to a public repo. Fill in
     WIFI_SSID / WIFI_PASSWORD below locally before flashing, and keep
     that edit uncommitted (or move it into a gitignored secrets.h).
     
   ===================================================================== */
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
/* ============================ 1. PIN MAP ============================ */
// ---- DHT22 Temperature & Humidity Sensor ----
#define DHT_PIN 4
#define DHT_TYPE DHT22
// ---- 1602 LCD with HW-061 I2C Adapter ----
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define LCD_I2C_ADDR 0x27  // Default address. Try 0x3F if LCD shows nothing.
// ---- 4-Channel Relay Module (AC Appliance Control) ----
#define RELAY1_PIN 16   // Relay 1: Bulb
#define RELAY2_PIN 17   // Relay 2: Socket
#define RELAY3_PIN 18   // Relay 3: Fan (Auto-controlled by DHT22)
#define RELAY4_PIN 19   // Relay 4: Multi-plug
const int RELAY_PINS[4] = { RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN };
const char* RELAY_NAMES[4] = { "Bulb", "Socket", "Fan", "Multi-plug" };
#define FAN_INDEX 2     // Array index of the Fan
// ---- Relay Active Level Configuration ----
// Most relay boards use Active LOW logic. Set this to true if yours is Active LOW.
// Set to false if your relay board turns ON when receiving a HIGH signal.
#define RELAY_ACTIVE_LOW true
/* ============================ 2. CONFIGURATION ======================= */
// WiFi Credentials
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
// Web Server instance listening on Port 80
WebServer server(80);
/* ============================ 3. GLOBAL OBJECTS ======================= */
DHT dht(DHT_PIN, DHT_TYPE);
// 16 characters, 2 lines LCD configuration
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 16, 2);
/* ============================ 4. GLOBAL STATE ========================= */
bool  relayState[4]   = { false, false, false, false };
float currentTemp      = 0.0;
float currentHumidity  = 0.0;
float tempThreshold     = 30.0;     // Fan automatically turns ON at/above this temp
bool  fanAutoMode       = true;     // Auto-control active
unsigned long lastSensorRead = 0;
unsigned long lastLcdSwitch  = 0;
int   lcdPage = 0;                  // Triggers screen switching (0: Temp, 1: Relays)
/* ============================ 5. FUNCTION PROTOTYPES ================== */
void initPins();
void initWiFi();
void initLCD();
void initDHT();
void initWebServer();
void readDHT22Sensor();
void autoFanControl();
void updateLCD();
void setRelay(int index, bool turnOn);
void toggleRelay(int index);
void handleRoot();
void handleToggleRelay();
void handleSetThreshold();
void handleSetAutoMode();
String buildDashboardHTML();
/* ============================ 6. SETUP / LOOP ========================= */
void setup() {
  Serial.begin(115200);
  Serial.println("\nInitializing Simplified Smart Home System...");
  // Initialize I2C Bus on default ESP32 pins
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  initPins();
  initLCD();
  initDHT();
  initWiFi();
  initWebServer();
  lcd.clear();
  lcd.print("System Ready");
  Serial.println("System fully loaded and operational.");
  delay(1500);
}
void loop() {
  server.handleClient();       // Listen for web requests
  
  // Read DHT22 and update auto fan logic every 2 seconds
  if (millis() - lastSensorRead > 2000) {
    readDHT22Sensor();
    autoFanControl();
    lastSensorRead = millis();
  }
  updateLCD();                 // Rotate and update LCD screens
}
/* ============================ 7. INITIALIZATION ======================= */
void initPins() {
  // Set Relay pins as output and set default state (OFF)
  for (int i = 0; i < 4; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], RELAY_ACTIVE_LOW ? HIGH : LOW); // High is OFF for active low
  }
}
void initLCD() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Booting...");
}
void initDHT() {
  dht.begin();
}
void initWiFi() {
  lcd.clear();
  lcd.print("Connecting WiFi");
  Serial.print("Connecting to: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retries = 0;
  
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(retries % 16, 1);
    lcd.print(".");
    retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    lcd.clear();
    lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi Connection Failed! Dashboard will be unavailable.");
    lcd.clear();
    lcd.print("WiFi Conn Failed");
    lcd.setCursor(0, 1);
    lcd.print("Offline Mode");
  }
  delay(2000);
}
void initWebServer() {
  server.on("/", handleRoot);
  server.on("/toggle", handleToggleRelay);
  server.on("/threshold", handleSetThreshold);
  server.on("/automode", handleSetAutoMode);
  server.begin();
  Serial.println("HTTP Server started.");
}
/* ============================ 8. INPUT CONTROL LOGIC ================== */
void readDHT22Sensor() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  // Validate sensor readouts (discard nan values)
  if (!isnan(h) && !isnan(t)) {
    currentHumidity = h;
    currentTemp = t;
  } else {
    Serial.println("Warning: Failed to read from DHT sensor!");
  }
}
void autoFanControl() {
  if (!fanAutoMode) return; // Skip if dashboard disabled auto mode
  // Automatically triggers Fan (RELAY3) based on the temperature threshold
  if (currentTemp >= tempThreshold && !relayState[FAN_INDEX]) {
    setRelay(FAN_INDEX, true);
    Serial.println("Auto mode: Fan turned ON (Threshold reached)");
  } else if (currentTemp < tempThreshold && relayState[FAN_INDEX]) {
    setRelay(FAN_INDEX, false);
    Serial.println("Auto mode: Fan turned OFF (Below threshold)");
  }
}
/* ============================ 9. RELAY CORE FUNCTIONS ================= */
void setRelay(int index, bool turnOn) {
  if (index < 0 || index > 3) return;
  relayState[index] = turnOn;
  
  // Output appropriate electrical level based on Active LOW or Active HIGH relay board
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(RELAY_PINS[index], turnOn ? LOW : HIGH);
  } else {
    digitalWrite(RELAY_PINS[index], turnOn ? HIGH : LOW);
  }
  Serial.print(RELAY_NAMES[index]);
  Serial.println(turnOn ? " -> ON" : " -> OFF");
}
void toggleRelay(int index) {
  setRelay(index, !relayState[index]);
}
/* ============================ 10. LCD DISPLAY DRIVER =================== */
void updateLCD() {
  // Toggle between LCD info pages every 3 seconds
  if (millis() - lastLcdSwitch > 3000) {
    lcdPage = (lcdPage + 1) % 2;
    lastLcdSwitch = millis();
    lcd.clear();
  }
  lcd.setCursor(0, 0);
  if (lcdPage == 0) {
    // Page 0: Temperature, Humidity, and Fan Mode
    lcd.print("Temp: ");
    lcd.print(currentTemp, 1);
    lcd.print("C");
    
    lcd.setCursor(0, 1);
    lcd.print("Hum : ");
    lcd.print(currentHumidity, 0);
    lcd.print("%  ");
    lcd.print(fanAutoMode ? "A" : "M");
  } else {
    // Page 1: Relay Status indicators
    // Abbreviations: Blb (Bulb), Skt (Socket), Fan (Fan), Mpl (Multi-plug)
    lcd.print("Blb:");
    lcd.print(relayState[0] ? "ON " : "OFF");
    lcd.print(" Skt:");
    lcd.print(relayState[1] ? "ON " : "OFF");
    lcd.setCursor(0, 1);
    lcd.print("Fan:");
    lcd.print(relayState[2] ? "ON " : "OFF");
    lcd.print(" Mpl:");
    lcd.print(relayState[3] ? "ON " : "OFF");
  }
}
/* ============================ 11. WEB DASHBOARD ROUTING ================ */
void handleRoot() {
  server.send(200, "text/html", buildDashboardHTML());
}
void handleToggleRelay() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id == FAN_INDEX) fanAutoMode = false; // Disable auto control if user explicitly toggles Fan
    toggleRelay(id);
  }
  
  // Return redirect to refresh the layout
  server.sendHeader("Location", "/");
  server.send(303);
}
void handleSetThreshold() {
  if (server.hasArg("value")) {
    tempThreshold = server.arg("value").toFloat();
  }
  
  server.sendHeader("Location", "/");
  server.send(303);
}
void handleSetAutoMode() {
  fanAutoMode = true;
  server.sendHeader("Location", "/");
  server.send(303);
}
// Generate the styling and layout for the Web Dashboard
String buildDashboardHTML() {
  String html = "<!DOCTYPE html><html><head>";
  // Refresh page content every 5 seconds to load updated readings
  html += "<meta http-equiv='refresh' content='5'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Smart Home Dashboard</title>";
  
  // Premium CSS styling
  html += "<style>";
  html += "@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;700&display=swap');";
  html += "* { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Inter', sans-serif; }";
  html += "body { background: linear-gradient(135deg, #0f172a 0%, #1e1b4b 100%); color: #f8fafc; min-height: 100vh; display: flex; flex-direction: column; align-items: center; justify-content: flex-start; padding: 20px; }";
  html += "h1 { margin: 20px 0 10px 0; font-size: 2.2rem; font-weight: 700; background: linear-gradient(135deg, #38bdf8 0%, #818cf8 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent; text-align: center; }";
  html += ".subtitle { color: #94a3b8; font-size: 1rem; margin-bottom: 30px; text-align: center; }";
  html += ".container { width: 100%; max-width: 600px; display: flex; flex-direction: column; gap: 20px; }";
  
  // Card elements
  html += ".card { background: rgba(30, 41, 59, 0.7); backdrop-filter: blur(12px); -webkit-backdrop-filter: blur(12px); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 16px; padding: 24px; box-shadow: 0 10px 30px rgba(0, 0, 0, 0.4); }";
  html += ".card h3 { font-size: 1.25rem; font-weight: 600; margin-bottom: 15px; color: #38bdf8; border-bottom: 1px solid rgba(255, 255, 255, 0.1); padding-bottom: 8px; }";
  
  // Sensors CSS
  html += ".sensor-row { display: flex; justify-content: space-around; text-align: center; margin-top: 15px; }";
  html += ".sensor-item { flex: 1; }";
  html += ".sensor-val { font-size: 2rem; font-weight: 700; color: #f1f5f9; margin-top: 5px; }";
  html += ".sensor-unit { font-size: 1rem; color: #94a3b8; font-weight: 400; }";
  
  // Appliances Controls CSS
  html += ".device-item { display: flex; justify-content: space-between; align-items: center; padding: 12px 0; border-bottom: 1px solid rgba(255, 255, 255, 0.05); }";
  html += ".device-item:last-child { border-bottom: none; }";
  html += ".device-info { display: flex; flex-direction: column; }";
  html += ".device-name { font-size: 1.1rem; font-weight: 600; color: #e2e8f0; }";
  html += ".device-status { font-size: 0.85rem; margin-top: 3px; font-weight: 500; }";
  html += ".status-on { color: #10b981; }";
  html += ".status-off { color: #ef4444; }";
  
  // Button & Form CSS
  html += ".btn-toggle { padding: 8px 18px; border: none; border-radius: 8px; background: linear-gradient(135deg, #0284c7 0%, #0369a1 100%); color: #fff; font-size: 0.95rem; font-weight: 600; cursor: pointer; transition: transform 0.1s ease; box-shadow: 0 4px 10px rgba(2, 132, 199, 0.3); }";
  html += ".btn-toggle:hover { transform: translateY(-1px); }";
  html += ".btn-toggle:active { transform: translateY(1px); }";
  
  html += ".form-row { display: flex; justify-content: space-between; align-items: center; margin-top: 10px; }";
  html += "input[type=number] { background: #0f172a; border: 1px solid rgba(255,255,255,0.15); border-radius: 8px; color: #fff; padding: 8px 12px; width: 80px; font-size: 1rem; text-align: center; }";
  html += "input[type=number]:focus { outline: none; border-color: #38bdf8; }";
  html += ".btn-set { padding: 8px 16px; border: none; border-radius: 8px; background: #38bdf8; color: #0f172a; font-weight: 700; font-size: 0.95rem; cursor: pointer; margin-left: 10px; }";
  html += ".btn-auto { display: block; width: 100%; padding: 10px; border: none; border-radius: 8px; background: linear-gradient(135deg, #10b981 0%, #059669 100%); color: white; font-weight: 700; font-size: 1rem; cursor: pointer; margin-top: 15px; text-align: center; box-shadow: 0 4px 12px rgba(16, 185, 129, 0.25); text-decoration: none; }";
  html += "</style></head><body>";
  html += "<h1>SMART HOME PORTAL</h1>";
  html += "<div class='subtitle'>ESP32 Home Automation Dashboard</div>";
  html += "<div class='container'>";
  // Card 1: Sensors
  html += "<div class='card'><h3>Sensor Information</h3>";
  html += "<div class='sensor-row'>";
  html += "<div class='sensor-item'><div>Temperature</div><div class='sensor-val'>" + String(currentTemp, 1) + "<span class='sensor-unit'>&deg;C</span></div></div>";
  html += "<div class='sensor-item'><div>Humidity</div><div class='sensor-val'>" + String(currentHumidity, 0) + "<span class='sensor-unit'>%</span></div></div>";
  html += "</div>";
  html += "</div>";
  // Card 2: Relay Controls
  html += "<div class='card'><h3>Appliance Status</h3>";
  for (int i = 0; i < 4; i++) {
    html += "<div class='device-item'>";
    html += "<div class='device-info'>";
    html += "<span class='device-name'>" + String(RELAY_NAMES[i]) + "</span>";
    
    // Display mode alongside Name for Fan
    if (i == FAN_INDEX) {
      html += "<span class='device-status'>Status: <span class='" + String(relayState[i] ? "status-on'>ON" : "status-off'>OFF") + "</span> | Mode: <span style='color:#a78bfa;'>" + String(fanAutoMode ? "AUTO" : "MANUAL") + "</span></span>";
    } else {
      html += "<span class='device-status'>Status: <span class='" + String(relayState[i] ? "status-on'>ON" : "status-off'>OFF") + "</span></span>";
    }
    
    html += "</div>";
    // Inline HTTP GET Form for Toggle control
    html += "<form action='/toggle' method='GET'>";
    html += "<input type='hidden' name='id' value='" + String(i) + "'>";
    html += "<button type='submit' class='btn-toggle'>Toggle</button></form>";
    html += "</div>";
  }
  html += "</div>";
  // Card 3: Auto Mode Settings
  html += "<div class='card'><h3>Auto Mode Settings</h3>";
  html += "<div class='form-row'>";
  html += "<span>Fan Threshold Temp:</span>";
  html += "<form action='/threshold' method='GET' style='display:flex; align-items:center;'>";
  html += "<input type='number' name='value' step='0.5' value='" + String(tempThreshold, 1) + "'>";
  html += "<button type='submit' class='btn-set'>Set</button></form>";
  html += "</div>";
  
  if (!fanAutoMode) {
    html += "<form action='/automode' method='GET'>";
    html += "<button type='submit' class='btn-auto'>Re-enable Fan Auto Mode</button></form>";
  }
  
  html += "</div>"; // end card
  html += "</div>"; // end container
  html += "</body></html>";
  
  return html;
}
