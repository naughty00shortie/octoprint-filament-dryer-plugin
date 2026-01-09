#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <DHT.h>
#include <ArduinoJson.h>

/* ===================== WiFi ===================== */
const char* ssid = "Girtjie";
const char* password = "Karelkat1";

/* ===================== OTA ====================== */
const char* otaHostname = "filament-dryer";
const char* otaPassword = "dryer123";

/* ===================== Pins ===================== */
int FAN_PIN     = 16;   // GPIO16
int HEATER_PIN  = 14;   // GPIO14
int DHT_PIN     = 4;    // GPIO4
#define LED_PIN     2    // GPIO2 (blue LED, inverted)

#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);

/* ===================== Settings ================= */
float TARGET_TEMP = 65.0;
float TOLERANCE   = 1.5;

const float MIN_TARGET = 25.0;
const float MAX_TARGET = 75.0;
const float OVERTEMP_CUTOFF = 75.0;

const unsigned long MAX_HEATER_ON_MS = 10UL * 60UL * 1000UL;
const int MAX_CONSEC_SENSOR_FAILS = 20;

/* ===================== State ==================== */
bool systemOn = false;
bool fanOn = false;
bool heaterOn = false;
bool safetyLatched = false;

unsigned long heaterOnSince = 0;
int sensorFailCount = 0;
String lastError = "";

/* ===================== History ================== */
struct HistoryEntry {
  unsigned long ts;
  float temp;
  float hum;
  bool fan;
  bool heater;
  bool system;
  bool latched;
};

const int HISTORY_SIZE = 300;
HistoryEntry history[HISTORY_SIZE];
int historyIndex = 0;

/* ===================== Server =================== */
ESP8266WebServer server(80);

/* ===================== Helpers ================== */
void pushHistory(float t, float h) {
  history[historyIndex] = {
    millis(),
    t,
    h,
    fanOn,
    heaterOn,
    systemOn,
    safetyLatched
  };
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
}

void setFan(bool on) {
  fanOn = on;
  digitalWrite(FAN_PIN, on ? HIGH : LOW);
}

void setHeater(bool on) {
  if (on) {
    setFan(true);
    if (!heaterOn) heaterOnSince = millis();
    heaterOn = true;
    digitalWrite(HEATER_PIN, HIGH);
  } else {
    heaterOn = false;
    heaterOnSince = 0;
    digitalWrite(HEATER_PIN, LOW);
  }
}

void resetSafety() {
  safetyLatched = false;
  sensorFailCount = 0;
  lastError = "";
}

/* ===================== Control Loop ============== */
void controlLoop() {
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    sensorFailCount++;
    setHeater(false);
    if (systemOn) setFan(true);
    lastError = "sensor_fail";
  } else {
    sensorFailCount = 0;

    if (systemOn && !safetyLatched) {
      if (temp >= OVERTEMP_CUTOFF) {
        safetyLatched = true;
        lastError = "overtemp";
        setHeater(false);
        setFan(true);
      } else {
        if (temp < TARGET_TEMP - TOLERANCE) {
          setHeater(true);
        } else if (temp > TARGET_TEMP + TOLERANCE) {
          setHeater(false);
        }

        if (heaterOn && (millis() - heaterOnSince > MAX_HEATER_ON_MS)) {
          safetyLatched = true;
          lastError = "heater_watchdog";
          setHeater(false);
          setFan(true);
        }
      }
    }
  }

  if (sensorFailCount >= MAX_CONSEC_SENSOR_FAILS) {
    safetyLatched = true;
    lastError = "sensor_lock";
    setHeater(false);
    setFan(true);
  }

  if (!systemOn) {
    setHeater(false);
    setFan(false);
  }

  pushHistory(temp, hum);
}

/* ===================== API ====================== */

void handleRoot() {
  String html;
  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Filament Dryer API</title>";
  html += "<style>";
  html += "body{font-family:Arial;background:#f4f4f4;margin:0;padding:20px;}";
  html += ".card{background:#fff;padding:20px;margin:auto;max-width:800px;";
  html += "border-radius:8px;box-shadow:0 2px 6px rgba(0,0,0,.2);}";
  html += "h1,h2{color:#333;}";
  html += "code{background:#eee;padding:4px;border-radius:4px;}";
  html += "pre{background:#eee;padding:10px;border-radius:6px;overflow:auto;}";
  html += "</style></head><body>";

  html += "<div class='card'>";
  html += "<h1>Filament Dryer Controller</h1>";
  html += "<p>Status: <b>";
  html += (WiFi.status() == WL_CONNECTED ? "Online" : "Offline");
  html += "</b></p>";

  html += "<h2>System State</h2>";
  html += "<p><b>GET</b> <code>/system</code></p>";
  html += "<p>Returns current dryer state.</p>";

  html += "<pre>{\n"
          "  \"system_on\": true,\n"
          "  \"fan_on\": true,\n"
          "  \"heater_on\": false,\n"
          "  \"safety_latched\": false,\n"
          "  \"last_error\": \"\"\n"
          "}</pre>";

  html += "<h2>Control System</h2>";
  html += "<p><b>POST</b> <code>/system</code></p>";
  html += "<p>Turn dryer ON or OFF. Turning ON clears safety latch.</p>";

  html += "<pre>{ \"on\": true }</pre>";

  html += "<h2>Settings</h2>";
  html += "<p><b>GET</b> <code>/settings</code></p>";
  html += "<p>Returns current configuration.</p>";

  html += "<pre>{\n"
          "  \"FAN_PIN\": 16,\n"
          "  \"HEATER_PIN\": 14,\n"
          "  \"DHT_PIN\": 4,\n"
          "  \"TARGET_TEMP\": 65.0,\n"
          "  \"TOLERANCE\": 1.5\n"
          "}</pre>";

  html += "<p><b>POST</b> <code>/settings</code></p>";
  html += "<p>Update configuration. Pin changes require system restart.</p>";

  html += "<pre>{\n"
          "  \"TARGET_TEMP\": 70.0,\n"
          "  \"TOLERANCE\": 2.0\n"
          "}</pre>";

  html += "<h2>History</h2>";
  html += "<p><b>GET</b> <code>/history</code></p>";
  html += "<p>Returns all stored history.</p>";

  html += "<p><b>GET</b> <code>/history?since=&lt;millis&gt;</code></p>";
  html += "<p>Returns only entries newer than the given timestamp.</p>";

  html += "<pre>{\n"
          "  \"ts\": 123456,\n"
          "  \"temp\": 54.2,\n"
          "  \"hum\": 21.5,\n"
          "  \"fan\": true,\n"
          "  \"heater\": false,\n"
          "  \"system\": true,\n"
          "  \"latched\": false\n"
          "}</pre>";

  html += "<h2>Notes</h2>";
  html += "<ul>";
  html += "<li>ESP8266 REST API</li>";
  html += "<li>DHT22 sensor</li>";
  html += "<li>OTA enabled</li>";
  html += "<li>Hostname: <b>";
  html += otaHostname;
  html += "</b></li>";
  html += "</ul>";

  html += "</div></body></html>";

  server.send(200, "text/html", html);
}


void handleSystem() {
  if (server.method() == HTTP_POST) {
    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{\"error\":\"bad_json\"}");
      return;
    }
    bool on = doc["on"];
    if (on) resetSafety();
    systemOn = on;
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    StaticJsonDocument<200> doc;
    doc["system_on"] = systemOn;
    doc["fan_on"] = fanOn;
    doc["heater_on"] = heaterOn;
    doc["safety_latched"] = safetyLatched;
    doc["last_error"] = lastError;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }
}

void handleHistory() {
  unsigned long since = server.hasArg("since") ? server.arg("since").toInt() : 0;
  StaticJsonDocument<4096> doc;
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < HISTORY_SIZE; i++) {
    HistoryEntry &e = history[i];
    if (e.ts > since) {
      JsonObject o = arr.createNestedObject();
      o["ts"] = e.ts;
      o["temp"] = e.temp;
      o["hum"] = e.hum;
      o["fan"] = e.fan;
      o["heater"] = e.heater;
      o["system"] = e.system;
      o["latched"] = e.latched;
    }
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleSettings() {
  if (server.method() == HTTP_POST) {
    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{\"error\":\"bad_json\"}");
      return;
    }

    bool needsPinReinit = false;
    int newFanPin = FAN_PIN;
    int newHeaterPin = HEATER_PIN;
    int newDhtPin = DHT_PIN;

    if (doc.containsKey("FAN_PIN")) {
      newFanPin = doc["FAN_PIN"];
      needsPinReinit = true;
    }
    if (doc.containsKey("HEATER_PIN")) {
      newHeaterPin = doc["HEATER_PIN"];
      needsPinReinit = true;
    }
    if (doc.containsKey("DHT_PIN")) {
      newDhtPin = doc["DHT_PIN"];
      needsPinReinit = true;
    }
    if (doc.containsKey("TARGET_TEMP")) {
      float newTarget = doc["TARGET_TEMP"];
      if (newTarget >= MIN_TARGET && newTarget <= MAX_TARGET) {
        TARGET_TEMP = newTarget;
      }
    }
    if (doc.containsKey("TOLERANCE")) {
      TOLERANCE = doc["TOLERANCE"];
    }

    if (needsPinReinit) {
      setHeater(false);
      setFan(false);
      systemOn = false;

      FAN_PIN = newFanPin;
      HEATER_PIN = newHeaterPin;
      DHT_PIN = newDhtPin;

      pinMode(FAN_PIN, OUTPUT);
      pinMode(HEATER_PIN, OUTPUT);
      digitalWrite(FAN_PIN, LOW);
      digitalWrite(HEATER_PIN, LOW);
    }

    StaticJsonDocument<200> resp;
    resp["status"] = "updated";
    JsonObject settings = resp.createNestedObject("settings");
    settings["FAN_PIN"] = FAN_PIN;
    settings["HEATER_PIN"] = HEATER_PIN;
    settings["DHT_PIN"] = DHT_PIN;
    settings["TARGET_TEMP"] = TARGET_TEMP;
    settings["TOLERANCE"] = TOLERANCE;

    String out;
    serializeJson(resp, out);
    server.send(200, "application/json", out);
  } else {
    StaticJsonDocument<200> doc;
    doc["FAN_PIN"] = FAN_PIN;
    doc["HEATER_PIN"] = HEATER_PIN;
    doc["DHT_PIN"] = DHT_PIN;
    doc["TARGET_TEMP"] = TARGET_TEMP;
    doc["TOLERANCE"] = TOLERANCE;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  }
}

/* ===================== Setup ==================== */
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED OFF (inverted)

  Serial.begin(115200);
  delay(2000);
  Serial.println("\nBooting filament dryer");

  pinMode(FAN_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  setFan(false);
  setHeater(false);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    yield();  // CRITICAL
    Serial.print(".");
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    if (millis() - start > 30000) break;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    Serial.print("ESP MAC address: ");
    Serial.println(WiFi.macAddress());

    digitalWrite(LED_PIN, LOW); // LED ON
  } else {
    Serial.println("\nWiFi FAILED");
    digitalWrite(LED_PIN, HIGH);
  }

  ArduinoOTA.setHostname(otaHostname);
  ArduinoOTA.setPassword(otaPassword);
  ArduinoOTA.begin();

  delay(2000);  // REQUIRED for DHT
  dht.begin();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/system", handleSystem);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/settings", handleSettings);
  server.begin();

  Serial.println("HTTP server started");
}

/* ===================== Loop ===================== */
void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  static unsigned long lastControl = 0;
  if (millis() - lastControl > 2000) {
    lastControl = millis();
    controlLoop();
  }

  yield();
}
