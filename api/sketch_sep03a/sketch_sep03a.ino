#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include "secrets.h"

/* ===================== WiFi ===================== */
// ssid / password / otaHostname / otaPassword now live in secrets.h (gitignored)

// Static IP configuration. Set all four octets of staticIP to 0 to use DHCP instead.
IPAddress staticIP(10, 0, 0, 50);    // ESP8266 static IP
IPAddress gateway(10, 0, 0, 2);      // Router IP
IPAddress subnet(255, 255, 255, 0);  // Subnet mask
IPAddress dns(10, 0, 0, 2);          // DNS server

/* ===================== Pins ===================== */
int FAN_PIN     = 14;   // GPIO14
int HEATER_PIN  = 16;   // GPIO16
int DHT_PIN     = 4;    // GPIO4
#define LED_PIN     2    // GPIO2 (blue LED, inverted)

#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);

/* ===================== Settings ================= */
float TARGET_TEMP = 55.0;
float TOLERANCE   = 1.5;

const float MIN_TARGET = 25.0;
// Hard user-mandated ceiling: actual temperature must never exceed 60C.
// MAX_TARGET is kept below that so normal hysteresis (target + TOLERANCE)
// can't itself approach 60, leaving OVERTEMP_CUTOFF as a real last-resort
// backstop rather than the thing normal operation brushes up against.
const float MAX_TARGET = 55.0;
const float OVERTEMP_CUTOFF = 60.0;

const unsigned long MAX_HEATER_ON_MS = 10UL * 60UL * 1000UL;
const int MAX_CONSEC_SENSOR_FAILS = 20;

// A sensor that fails intermittently - often enough to be unreliable, but
// recovering just often enough to keep resetting the consecutive-failure
// counter above - would otherwise never trip the safety latch. This tracks
// failures over a rolling window instead: if at least half of the last 20
// read cycles (~50s) failed, that counts as "erroring for too long" even
// though none of them were 20-in-a-row.
const int RELIABILITY_WINDOW = 20;
const int RELIABILITY_FAIL_THRESHOLD = 10;

// DHT22 needs >=2000ms between reads; add margin so millis() jitter never
// triggers a read before the sensor has refreshed (a common cause of
// spurious "sensor_fail" readings).
const unsigned long SENSOR_READ_INTERVAL_MS = 2500UL;
const unsigned long WIFI_CHECK_INTERVAL_MS = 30UL * 1000UL;

// Safety net for the /diag/relay manual override below: auto-revert to
// normal control if nobody's touched it in a while, so a debugging session
// can't accidentally leave the dryer in manual mode indefinitely.
const unsigned long OVERRIDE_TIMEOUT_MS = 10UL * 60UL * 1000UL;

/* ===================== State ==================== */
bool systemOn = false;
bool fanOn = false;
bool heaterOn = false;
bool safetyLatched = false;

// Diagnostic-only: lets fan/heater be driven independently of the normal
// control loop, to isolate whether sensor dropouts track the fan
// specifically, the heater relay, or general power draw. See /diag/relay.
bool manualOverride = false;
unsigned long overrideLastSetAt = 0;

unsigned long heaterOnSince = 0;
int sensorFailCount = 0;
String lastError = "";

bool recentReadFailed[RELIABILITY_WINDOW] = {false};
int recentReadIndex = 0;
int recentFailSum = 0;

/* ===================== History ================== */
struct HistoryEntry {
  unsigned long ts;
  float temp;
  float hum;
  float target;
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
// DHT22 range is -40..80C / 0..100%RH. Anything outside that is a bad
// read that didn't happen to come back as NaN (loose wiring / brownout
// can produce garbage values instead of NaN).
bool isValidReading(float t, float h) {
  if (isnan(t) || isnan(h)) return false;
  if (h < 0.0 || h > 100.0) return false;
  if (t < -40.0 || t > 80.0) return false;
  return true;
}

void pushHistory(float t, float h) {
  history[historyIndex] = {
    millis(),
    t,
    h,
    TARGET_TEMP,
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
  Serial.print("Fan ");
  Serial.print(on ? "ON" : "OFF");
  Serial.print(" - GPIO");
  Serial.print(FAN_PIN);
  Serial.print(" = ");
  Serial.println(on ? "HIGH" : "LOW");
}

void setHeater(bool on) {
  if (on) {
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
  for (int i = 0; i < RELIABILITY_WINDOW; i++) {
    recentReadFailed[i] = false;
  }
  recentReadIndex = 0;
  recentFailSum = 0;
}

/* ===================== Control Loop ============== */
void controlLoop() {
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  // DHT22 occasionally glitches on a single read (timing-sensitive bit-bang
  // protocol, easily disrupted by WiFi/OTA interrupts). Retry once before
  // counting it as a real failure.
  if (!isValidReading(temp, hum)) {
    delay(50);
    temp = dht.readTemperature();
    hum  = dht.readHumidity();
  }

  // Hard overtemp cutoff applies unconditionally, even mid manual-override
  // test - the 60C ceiling is non-negotiable regardless of what's being
  // diagnosed.
  if (isValidReading(temp, hum) && temp >= OVERTEMP_CUTOFF) {
    setHeater(false);
    safetyLatched = true;
    lastError = "overtemp";
    manualOverride = false;
  }

  if (manualOverride) {
    if (millis() - overrideLastSetAt > OVERRIDE_TIMEOUT_MS) {
      manualOverride = false;
    } else {
      // Diagnostic mode: fan/heater stay exactly as /diag/relay last set
      // them - skip normal on/off and hysteresis control entirely so fan-
      // only vs heater-only vs both can be tested in isolation. Still logs
      // real readings so the results are comparable afterward.
      pushHistory(temp, hum);
      return;
    }
  }

  // Handle system on/off first
  if (!systemOn) {
    // System is OFF - turn everything off
    setHeater(false);
    setFan(false);
    digitalWrite(LED_PIN, HIGH); // LED OFF (inverted)
    pushHistory(temp, hum);
    return;
  }

  // System is ON - fan should ALWAYS be on
  setFan(true);
  digitalWrite(LED_PIN, LOW); // LED ON (inverted)

  // Check for sensor failures
  bool readOk = isValidReading(temp, hum);

  // Track reliability over a rolling window in addition to the consecutive
  // counter - a sensor that fails about half its reads (recovering just
  // often enough to keep resetting the consecutive counter to zero) should
  // still be treated as untrustworthy, not just one that misses 20 in a row.
  recentFailSum -= recentReadFailed[recentReadIndex] ? 1 : 0;
  recentReadFailed[recentReadIndex] = !readOk;
  recentFailSum += !readOk ? 1 : 0;
  recentReadIndex = (recentReadIndex + 1) % RELIABILITY_WINDOW;

  if (!readOk) {
    sensorFailCount++;
  } else {
    sensorFailCount = 0;
  }

  // Whether the sensor can be trusted right now. Deliberately NOT a sticky
  // latch (unlike overtemp/heater_watchdog below): once reads are reliable
  // again this clears on its own and normal control resumes, since a sensor
  // blip that's already over and gone doesn't need a human to intervene.
  bool sensorUnreliable = (sensorFailCount >= MAX_CONSEC_SENSOR_FAILS) ||
                          (recentFailSum >= RELIABILITY_FAIL_THRESHOLD);

  if (!readOk || sensorUnreliable) {
    setHeater(false);  // Don't act on a reading we can't trust
    lastError = sensorUnreliable ? "sensor_unreliable" : "sensor_fail";
    if (!readOk) {
      Serial.print("Sensor read failed (raw temp=");
      Serial.print(temp);
      Serial.print(", hum=");
      Serial.print(hum);
      Serial.print("), consecutive fails=");
      Serial.print(sensorFailCount);
      Serial.print(", recent fails in window=");
      Serial.println(recentFailSum);
    }
  } else {
    // Sensor just came back AND the recent window has cooled down too -
    // clear any lingering sensor-error status and resume normal control.
    if (lastError == "sensor_fail" || lastError == "sensor_unreliable") {
      lastError = "";
    }

    // Only control heater if not safety latched (overtemp/watchdog - these
    // do require an explicit restart, unlike the sensor checks above)
    if (!safetyLatched) {
      // Check for overtemp
      if (temp >= OVERTEMP_CUTOFF) {
        safetyLatched = true;
        lastError = "overtemp";
        setHeater(false);
      } else {
        // Normal temperature control with hysteresis
        if (temp < TARGET_TEMP - TOLERANCE) {
          setHeater(true);
        } else if (temp > TARGET_TEMP + TOLERANCE) {
          setHeater(false);
        }
        // Note: Between (TARGET_TEMP - TOLERANCE) and (TARGET_TEMP + TOLERANCE),
        // heater state doesn't change (hysteresis)

        // Check heater watchdog timer
        if (heaterOn && (millis() - heaterOnSince > MAX_HEATER_ON_MS)) {
          safetyLatched = true;
          lastError = "heater_watchdog";
          setHeater(false);
        }
      }
    } else {
      // Safety latched - keep heater off
      setHeater(false);
    }
  }

  pushHistory(temp, hum);
}

/* ===================== API ====================== */

// Add CORS headers to allow browser access from OctoPrint
void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Cache-Control");
  server.sendHeader("Access-Control-Max-Age", "86400");
}

// Handle OPTIONS preflight requests
void handleOptions() {
  addCorsHeaders();
  server.send(200, "text/plain", "");
}

void handleRoot() {
  addCorsHeaders();
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
          "  \"FAN_PIN\": 14,\n"
          "  \"HEATER_PIN\": 16,\n"
          "  \"DHT_PIN\": 4,\n"
          "  \"TARGET_TEMP\": 55.0,\n"
          "  \"TOLERANCE\": 1.5\n"
          "}</pre>";

  html += "<p><b>POST</b> <code>/settings</code></p>";
  html += "<p>Update configuration. Pin changes require system restart. TARGET_TEMP is capped at 55C (hard 60C overtemp cutoff).</p>";

  html += "<pre>{\n"
          "  \"TARGET_TEMP\": 50.0,\n"
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
  addCorsHeaders();
  if (server.method() == HTTP_POST) {
    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{\"error\":\"bad_json\"}");
      return;
    }
    bool on = doc["on"];
    systemOn = on;

    if (on) {
      // System turning ON - clear safety and start fan immediately
      resetSafety();
      setFan(true);
      digitalWrite(LED_PIN, LOW); // LED ON
    } else {
      // System turning OFF - stop everything immediately
      setHeater(false);
      setFan(false);
      digitalWrite(LED_PIN, HIGH); // LED OFF
    }

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
  addCorsHeaders();
  unsigned long since = server.hasArg("since") ? server.arg("since").toInt() : 0;

  // Log memory before starting
  Serial.print("History request - Free heap: ");
  Serial.println(ESP.getFreeHeap());

  // Build JSON manually to avoid large heap allocations
  String json = "[";
  int added = 0;

  // Iterate through circular buffer in chronological order
  // Start from historyIndex (oldest entry) and wrap around
  for (int i = 0; i < HISTORY_SIZE; i++) {
    int idx = (historyIndex + i) % HISTORY_SIZE;
    HistoryEntry &e = history[idx];

    // Skip uninitialized entries (ts == 0) and entries older than 'since'
    if (e.ts > 0 && e.ts > since) {
      if (added > 0) json += ",";

      // Manually construct JSON for this entry to avoid ArduinoJson overhead.
      // NaN readings must serialize as JSON null, not a bare `nan` token -
      // String(NAN, 1) produces the literal text "nan" unquoted, which is
      // NOT valid JSON (RFC 8259 has no NaN literal). A client's JSON.parse
      // throws on it, which silently poisons the *entire* array for
      // whichever poll request happens to include one bad entry - not just
      // that one reading.
      json += "{";
      json += "\"ts\":"; json += String(e.ts); json += ",";
      json += "\"temp\":"; json += (isnan(e.temp) ? String("null") : String(e.temp, 1)); json += ",";
      json += "\"hum\":"; json += (isnan(e.hum) ? String("null") : String(e.hum, 1)); json += ",";
      json += "\"target\":"; json += String(e.target, 1); json += ",";
      json += "\"fan\":"; json += e.fan ? "true" : "false"; json += ",";
      json += "\"heater\":"; json += e.heater ? "true" : "false"; json += ",";
      json += "\"system\":"; json += e.system ? "true" : "false"; json += ",";
      json += "\"latched\":"; json += e.latched ? "true" : "false";
      json += "}";

      added++;

      // Stop if JSON gets too large (prevent OOM)
      if (json.length() > 15000) {
        Serial.println("WARNING: History truncated due to size limit");
        break;
      }
    }
  }
  json += "]";

  Serial.print("Sent ");
  Serial.print(added);
  Serial.print(" entries, JSON size: ");
  Serial.print(json.length());
  Serial.print(" bytes, Free heap after: ");
  Serial.println(ESP.getFreeHeap());

  server.send(200, "application/json", json);
}

// Bypasses the DHT library to measure the raw protocol timing on DHT_PIN.
// Every read has been coming back NaN since boot even with a retry, which
// rules out a transient glitch - either nothing is answering the start
// signal at all (dead sensor / no power / wrong pin / no pull-up), or
// something answers but the 40-bit payload gets corrupted partway through
// (noise/interference/wiring). This decodes the full handshake twice so we
// can see exactly where - if anywhere - it breaks down.
//
// Deliberately does NOT disable interrupts around the bit-bang read (unlike
// the DHT library itself): if the sensor never responds, worst case here is
// blocking ~40 bits x a few ms of timeout, and doing that with interrupts
// off risks a WiFi-stack stall or watchdog reset. Coarse presence/absence
// is the goal, not microsecond-perfect timing.
void handleDhtDiag() {
  addCorsHeaders();
  DynamicJsonDocument doc(2048);

  // No pull-up at all: if this floats/reads LOW, there's no external
  // pull-up resistor on the line (expected on a bare DHT22; most 3-pin
  // breakout modules include one already).
  pinMode(DHT_PIN, INPUT);
  delay(10);
  doc["floating_level_no_pullup"] = digitalRead(DHT_PIN);

  pinMode(DHT_PIN, INPUT_PULLUP);
  delay(5);
  doc["idle_level_with_internal_pullup"] = digitalRead(DHT_PIN);

  JsonArray attempts = doc.createNestedArray("attempts");

  for (int attempt = 0; attempt < 2; attempt++) {
    JsonObject a = attempts.createNestedObject();

    // DHT22 start sequence: host pulls the line low for >=1ms, then
    // releases so the sensor can pull it low itself to acknowledge.
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, LOW);
    delay(2);
    pinMode(DHT_PIN, INPUT_PULLUP);

    unsigned long ackLowUs = pulseIn(DHT_PIN, LOW, 50000);
    unsigned long ackHighUs = pulseIn(DHT_PIN, HIGH, 50000);

    uint8_t data[5] = {0, 0, 0, 0, 0};
    int bitsRead = 0;
    JsonArray firstBits = a.createNestedArray("first_bits_low_high_us");

    for (int i = 0; i < 40; i++) {
      unsigned long lowUs = pulseIn(DHT_PIN, LOW, 5000);
      unsigned long highUs = pulseIn(DHT_PIN, HIGH, 5000);
      if (lowUs == 0 || highUs == 0) break;  // no more signal - stop here
      bitsRead++;
      int bit = (highUs > 40) ? 1 : 0;  // ~26-28us => 0, ~70us => 1
      data[i / 8] = (data[i / 8] << 1) | bit;
      if (firstBits.size() < 8) {
        JsonObject bt = firstBits.createNestedObject();
        bt["low"] = lowUs;
        bt["high"] = highUs;
      }
    }

    bool checksumOk = (bitsRead == 40) && (uint8_t)(data[0] + data[1] + data[2] + data[3]) == data[4];

    a["ack_low_us"] = ackLowUs;
    a["ack_high_us"] = ackHighUs;
    a["bits_read"] = bitsRead;
    a["checksum_ok"] = checksumOk;
    if (bitsRead == 40) {
      char hexBuf[11];
      snprintf(hexBuf, sizeof(hexBuf), "%02X%02X%02X%02X%02X", data[0], data[1], data[2], data[3], data[4]);
      a["raw_bytes"] = hexBuf;
    }

    delay(2200);  // respect DHT22's minimum interval before the next attempt
  }

  doc["note"] = "bits_read should reach 40 with checksum_ok=true for a real reading; ack pulses near 0 or bits_read=0 across both attempts means nothing is answering the line at all";

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);

  // Hand the pin back to the DHT library's expected state.
  dht.begin();
}

// Manually drives the fan/heater relays independently of the normal control
// loop, to isolate whether sensor dropouts track the fan specifically, the
// heater relay, or general power draw - system_on currently energizes both
// together, so field data alone can't tell them apart. POST {"override":
// true, "fan": true/false, "heater": true/false} to test a combination;
// POST {"override": false} to hand control back to normal operation.
// Auto-reverts after OVERRIDE_TIMEOUT_MS regardless, and the hard overtemp
// cutoff always applies even while active (see controlLoop).
void handleDiagRelay() {
  addCorsHeaders();
  if (server.method() == HTTP_POST) {
    StaticJsonDocument<200> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
      server.send(400, "application/json", "{\"error\":\"bad_json\"}");
      return;
    }

    if (doc.containsKey("override")) {
      manualOverride = doc["override"];
      overrideLastSetAt = millis();
    }
    if (manualOverride) {
      if (doc.containsKey("fan")) {
        setFan(doc["fan"]);
      }
      if (doc.containsKey("heater")) {
        bool wantHeater = doc["heater"];
        if (wantHeater) setFan(true);  // never run the heater without airflow
        setHeater(wantHeater);
      }
    }
  }

  StaticJsonDocument<200> resp;
  resp["override_active"] = manualOverride;
  resp["fan_on"] = fanOn;
  resp["heater_on"] = heaterOn;
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
}

void handleSettings() {
  addCorsHeaders();
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

  // Initialize history array to prevent garbage values
  for (int i = 0; i < HISTORY_SIZE; i++) {
    history[i].ts = 0;
    history[i].temp = 0.0;
    history[i].hum = 0.0;
    history[i].target = TARGET_TEMP;
    history[i].fan = false;
    history[i].heater = false;
    history[i].system = false;
    history[i].latched = false;
  }

  pinMode(FAN_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  setFan(false);
  setHeater(false);

  WiFi.mode(WIFI_STA);

  // Apply static IP only if one was actually configured (all-zero staticIP means "use DHCP").
  if (staticIP[0] != 0 || staticIP[1] != 0 || staticIP[2] != 0 || staticIP[3] != 0) {
    WiFi.config(staticIP, gateway, subnet, dns);
  }

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

    digitalWrite(LED_PIN, HIGH); // LED OFF (inverted, will turn on when system starts)
  } else {
    Serial.println("\nWiFi FAILED");
    digitalWrite(LED_PIN, HIGH); // LED OFF
  }

  ArduinoOTA.setHostname(otaHostname);
  ArduinoOTA.setPassword(otaPassword);
  ArduinoOTA.begin();

  delay(2000);  // REQUIRED for DHT
  dht.begin();

  // Register API endpoints
  server.on("/", HTTP_GET, handleRoot);
  server.on("/system", handleSystem);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/settings", handleSettings);
  server.on("/diag/dht", HTTP_GET, handleDhtDiag);
  server.on("/diag/relay", HTTP_GET, handleDiagRelay);
  server.on("/diag/relay", HTTP_POST, handleDiagRelay);

  // Register OPTIONS handlers for CORS preflight
  server.on("/system", HTTP_OPTIONS, handleOptions);
  server.on("/history", HTTP_OPTIONS, handleOptions);
  server.on("/settings", HTTP_OPTIONS, handleOptions);
  server.on("/diag/dht", HTTP_OPTIONS, handleOptions);
  server.on("/diag/relay", HTTP_OPTIONS, handleOptions);

  server.begin();

  Serial.println("HTTP server started");
}

/* ===================== Loop ===================== */
void loop() {
  ArduinoOTA.handle();
  server.handleClient();

  static unsigned long lastControl = 0;
  if (millis() - lastControl > SENSOR_READ_INTERVAL_MS) {
    lastControl = millis();
    controlLoop();
  }

  // WiFi can silently drop out from under a long-running ESP8266; without
  // this the API and OTA both go dark until a manual power cycle.
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > WIFI_CHECK_INTERVAL_MS) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, reconnecting...");
      WiFi.reconnect();
    }
  }

  yield();
}
