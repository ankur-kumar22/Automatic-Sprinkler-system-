#define BLYNK_TEMPLATE_ID   "TMPLxxxxxxxx"      // from your template
#define BLYNK_TEMPLATE_NAME "Plant Waterer"
#define BLYNK_AUTH_TOKEN    "YOUR_AUTH_TOKEN"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ---------- Pins ----------
#define SENSOR_AO  6     // moisture sensor AO
#define RELAY_PIN  24    // relay IN (active-low module)

// ---------- Calibration ----------
const int DRY_VALUE = 3350;
const int WET_VALUE = 1300;

// ---------- Watering timing ----------
const unsigned long PUMP_BURST_MS   = 5000;      // pump on per burst
const unsigned long SOAK_MS         = 180000;    // 3 min wait after each burst
const unsigned long CHECK_MS        = 30000;     // soil check when idle
const int           MAX_BURSTS      = 4;         // per session
const unsigned long FAULT_LOCKOUT_MS = 7200000;  // 2 h if target never reached

// Set true to test with no pump switching
const bool DRY_RUN = false;

// ---------- Settings (changed from the app) ----------
int  startPct  = 30;
int  stopPct   = 55;
bool autoMode  = true;

// ---------- State ----------
enum State { IDLE, PUMPING, SOAKING };
State state = IDLE;
bool manualBurst = false;
int  burstCount = 0;
unsigned long stateStart = 0;
unsigned long lastCheck = 0;
unsigned long lockoutStart = 0;
unsigned long lockoutLen = 0;

BlynkTimer timer;

// ---------- Relay (active-low, release pin for OFF) ----------
void relayOn()  { pinMode(RELAY_PIN, OUTPUT); digitalWrite(RELAY_PIN, LOW); }
void relayOff() { pinMode(RELAY_PIN, INPUT); }

void setStatus(const char* s) {
  Serial.println(s);
  if (Blynk.connected()) Blynk.virtualWrite(V6, s);
}

int readMoisture() {
  long sum = 0;
  for (int i = 0; i < 50; i++) { sum += analogRead(SENSOR_AO); delay(5); }
  int raw = sum / 50;
  int pct = constrain(map(raw, DRY_VALUE, WET_VALUE, 0, 100), 0, 100);
  Serial.printf("raw=%d  moisture=%d%%\n", raw, pct);
  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, pct);
    Blynk.virtualWrite(V7, raw);
  }
  return pct;
}

void startPump() {
  if (!DRY_RUN) relayOn();
  state = PUMPING;
  stateStart = millis();
  if (Blynk.connected()) Blynk.virtualWrite(V1, 1);
  char buf[40];
  snprintf(buf, sizeof(buf), "Pumping (burst %d/%d)", burstCount + 1, MAX_BURSTS);
  setStatus(buf);
}

void stopPump() {
  relayOff();
  if (Blynk.connected()) Blynk.virtualWrite(V1, 0);
}

void endSession(bool faulted) {
  state = IDLE;
  lastCheck = millis();
  if (faulted) {
    lockoutStart = millis();
    lockoutLen = FAULT_LOCKOUT_MS;
    setStatus("Target not reached - locked 2h. Check tank/sensor");
    if (Blynk.connected()) Blynk.logEvent("pump_fault", "Soil did not reach target. Check tank and sensor.");
  } else {
    lockoutLen = 0;
    setStatus("Done - soil moist enough");
  }
}

void runWatering() {
  unsigned long now = millis();
  switch (state) {
    case IDLE:
      if (now - lastCheck >= CHECK_MS) {
        lastCheck = now;
        int m = readMoisture();
        bool locked = (lockoutLen > 0) && (now - lockoutStart < lockoutLen);
        if (autoMode && m < startPct && !locked) {
          burstCount = 0;
          manualBurst = false;
          startPump();
        } else if (locked) {
          setStatus("Locked out (fault protection)");
        } else {
          setStatus(autoMode ? "Idle - soil OK" : "Idle - auto mode off");
        }
      }
      break;

    case PUMPING:
      if (now - stateStart >= PUMP_BURST_MS) {
        stopPump();
        burstCount++;
        if (manualBurst) {
          manualBurst = false;
          state = IDLE;
          lastCheck = now;
          setStatus("Manual burst done");
        } else {
          state = SOAKING;
          stateStart = now;
          setStatus("Soaking - waiting for water to spread");
        }
      }
      break;

    case SOAKING:
      if (now - stateStart >= SOAK_MS) {
        int m = readMoisture();
        if (m >= stopPct)               endSession(false);
        else if (burstCount >= MAX_BURSTS) endSession(true);
        else                            startPump();
      }
      break;
  }
}

// ---------- Blynk handlers ----------
BLYNK_CONNECTED() {
  Blynk.syncVirtual(V3, V4, V5);       // load saved settings from the app
  Blynk.virtualWrite(V1, state == PUMPING ? 1 : 0);
}

BLYNK_WRITE(V2) {                       // manual one-burst button
  if (param.asInt() == 1) {
    if (state == IDLE) {
      manualBurst = true;
      burstCount = 0;
      startPump();
    } else {
      setStatus("Busy - manual burst ignored");
    }
    Blynk.virtualWrite(V2, 0);
  }
}

BLYNK_WRITE(V3) { autoMode = param.asInt(); }

BLYNK_WRITE(V4) {
  int v = param.asInt();
  startPct = constrain(v, 5, 80);
  if (stopPct < startPct + 10) stopPct = startPct + 10;   // keep a gap
}

BLYNK_WRITE(V5) {
  int v = param.asInt();
  stopPct = constrain(v, startPct + 10, 95);
}

// ---------- Wi-Fi / Blynk connection (never blocks watering for long) ----------
void keepConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    return;
  }
  if (!Blynk.connected()) Blynk.connect(3000);
}

void setup() {
  relayOff();                           // pump off from the very start
  Serial.begin(115200);
  delay(2000);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  Serial.println(DRY_RUN ? "DRY RUN: pump disabled" : "LIVE: pump enabled");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Blynk.config(BLYNK_AUTH_TOKEN);

  timer.setInterval(15000L, keepConnected);
  lastCheck = millis() - CHECK_MS + 5000;   // first soil check after 5 s
}

void loop() {
  if (Blynk.connected()) Blynk.run();
  timer.run();
  runWatering();
}
