# Plant Watering Project: All Test Code

Run these in order. Each sketch is standalone, so upload one at a time and open the Serial Monitor at **115200 baud**.

| # | Test | Mains needed? |
|---|---|---|
| 1 | ADC pin diagnostic | No |
| 2 | Moisture sensor raw test | No |
| 3 | Moisture sensor with percentage | No |
| 4 | Relay click test | No |
| 5 | Terminal check (NO vs NC) | No |
| 6 | Watering logic (no Blynk), with DRY_RUN | Only for the bucket test |
| 7 | Final sketch with Blynk | Yes |

Board: **ESP32C5 Dev Module**, ESP32 Arduino core 3.3.0 or newer. If Serial shows nothing, set **USB CDC On Boot: Enabled** and press RST.

---

## Wiring used in all tests

| Device pin | ESP32-C5 |
|---|---|
| Sensor VCC | 3V3 |
| Sensor GND | GND |
| Sensor AO | GPIO6 |
| Relay VCC | 5V |
| Relay GND | GND |
| Relay IN | GPIO24 |

---

## Test 1: ADC pin diagnostic

Use this when readings look wrong. Touch a jumper from 3V3 to each pin and see which value jumps to about 3000+ mV. It also catches loose connections.

```cpp
void setup() {
  Serial.begin(115200);
  delay(2000);
  analogReadResolution(12);
  for (int p = 1; p <= 6; p++) pinMode(p, INPUT);
  Serial.println("ADC pin test: touch a wire from 3V3 to each pin");
}

void loop() {
  Serial.printf("G1=%4d  G2=%4d  G3=%4d  G4=%4d  G5=%4d  G6=%4d  (mV)\n",
    analogReadMilliVolts(1), analogReadMilliVolts(2), analogReadMilliVolts(3),
    analogReadMilliVolts(4), analogReadMilliVolts(5), analogReadMilliVolts(6));
  delay(500);
}
```

Expected: the pin you touch to 3V3 jumps to about 3000+ mV, and to GND drops to about 0.

---

## Test 2: Moisture sensor raw values (calibration)

Gives `avg`, `min` and `max`. A good connection has min and max within about 10-30 of each other.

```cpp
#define SENSOR_AO   6    // AO wire on GPIO6
#define SENSOR_DO   5    // optional, leave unconnected if unused

void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(SENSOR_DO, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  Serial.println("Soil moisture test (VCC on 3.3V, AO on GPIO6)");
}

void loop() {
  long sum = 0;
  int minV = 4095, maxV = 0;
  for (int i = 0; i < 50; i++) {
    int v = analogRead(SENSOR_AO);
    sum += v;
    if (v < minV) minV = v;
    if (v > maxV) maxV = v;
    delay(5);
  }
  Serial.printf("avg=%d  min=%d  max=%d  DO=%d\n", sum / 50, minV, maxV, digitalRead(SENSOR_DO));
  delay(1000);
}
```

Calibration steps:
1. Probe in **air**, wait about 20 s, note `avg` (your value was about **3350**).
2. Probe tip in **water** (keep the blue module dry), note `avg` (your value was about **1300**).
3. Higher value = drier. DO is ignored (DO reads a random value if GPIO5 is unconnected).

---

## Test 3: Moisture percentage

Uses your calibrated values. Expect about 0% in air and about 100% in water.

```cpp
#define SENSOR_AO   6

const int DRY_VALUE = 3350;
const int WET_VALUE = 1300;

void setup() {
  Serial.begin(115200);
  delay(2000);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  Serial.println("Soil moisture with percentage");
}

void loop() {
  long sum = 0;
  for (int i = 0; i < 50; i++) {
    sum += analogRead(SENSOR_AO);
    delay(5);
  }
  int raw = sum / 50;
  int pct = constrain(map(raw, DRY_VALUE, WET_VALUE, 0, 100), 0, 100);
  Serial.printf("raw=%d  moisture=%d%%\n", raw, pct);
  delay(1000);
}
```

Then push the probe into your plant's dry soil and again after watering. Use those percentages to set the start and stop thresholds.

---

## Test 4: Relay click test

Mains unplugged and nothing on the screw terminals. The module is **active-low**, so the pin is pulled LOW for ON and released (INPUT) for OFF.

```cpp
#define RELAY_PIN  24

void relayOn() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);    // pull IN low -> relay ON
}

void relayOff() {
  pinMode(RELAY_PIN, INPUT);       // release IN -> relay OFF, LED fully off
}

void setup() {
  relayOff();                      // off from the very start
  Serial.begin(115200);
  delay(2000);
  Serial.println("Relay test: 2 s ON, 2 s OFF");
}

void loop() {
  relayOn();
  Serial.println("Relay ON");
  delay(2000);
  relayOff();
  Serial.println("Relay OFF");
  delay(2000);
}
```

Expected: a click and a fully lit LED on ON, a second click and a fully off LED on OFF. If the LED is only dim when "off", you are driving the pin HIGH instead of releasing it.

---

## Test 5: Terminal check (NO vs NC)

Do this before connecting mains. Run Test 4, then with a multimeter in continuity mode (mains unplugged):

- COM (middle) to the top terminal should beep **only while the relay is ON**. Then top = NO.
- If it beeps while the relay is OFF, that terminal is NC. Use the other outer terminal as NO.

Without a multimeter: connect ESP32 **5V** to COM, then an LED with a 220-470 ohm resistor from the test terminal to GND. The LED should light only during "Relay ON" for NO.

---

## Test 6: Watering logic without Blynk (with DRY_RUN)

Stages:
1. `DRY_RUN = true`, mains unplugged: Serial prints bursts, but the relay never clicks.
2. `DRY_RUN = false`, mains unplugged: two clean clicks per burst.
3. Bucket test with mains: pump submerged in water, `DRY_RUN = false`.

The timings below are shortened for testing. Restore the real values (in comments) afterwards.

```cpp
#define SENSOR_AO   6    // moisture sensor AO
#define RELAY_PIN   24   // relay IN (active-low module)

// Calibration
const int DRY_VALUE = 3350;
const int WET_VALUE = 1300;

// Watering rules
const int START_PCT = 30;                    // water when moisture falls below this
const int STOP_PCT  = 55;                    // stop when it reaches this
const unsigned long PUMP_BURST_MS = 5000;    // pump time per burst
const int MAX_BURSTS              = 4;       // max bursts per session

// TEST timings (short). Real values in comments.
const unsigned long SOAK_MS    = 10000;      // real: 180000 (3 min)
const unsigned long LOCKOUT_MS = 60000;      // real: 3600000 (1 h)
const unsigned long CHECK_MS   = 5000;       // real: 30000 (30 s)

// true = prints "Pump ON/OFF" but the relay never switches
const bool DRY_RUN = true;

unsigned long lastSession = 0;
bool everWatered = false;

void relayOn()  { pinMode(RELAY_PIN, OUTPUT); digitalWrite(RELAY_PIN, LOW); }
void relayOff() { pinMode(RELAY_PIN, INPUT); }

int readMoisturePct() {
  long sum = 0;
  for (int i = 0; i < 50; i++) { sum += analogRead(SENSOR_AO); delay(5); }
  int raw = sum / 50;
  int pct = constrain(map(raw, DRY_VALUE, WET_VALUE, 0, 100), 0, 100);
  Serial.printf("raw=%d  moisture=%d%%\n", raw, pct);
  return pct;
}

void pumpBurst() {
  Serial.println("Pump ON");
  if (!DRY_RUN) relayOn();
  delay(PUMP_BURST_MS);
  relayOff();
  Serial.println("Pump OFF");
}

void waterSession() {
  for (int i = 1; i <= MAX_BURSTS; i++) {
    Serial.printf("Burst %d of %d\n", i, MAX_BURSTS);
    pumpBurst();
    delay(SOAK_MS);                       // let water soak in before measuring
    if (readMoisturePct() >= STOP_PCT) break;
  }
  relayOff();
  lastSession = millis();
  everWatered = true;
}

void setup() {
  relayOff();                             // pump off from the very start
  Serial.begin(115200);
  delay(2000);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  Serial.println(DRY_RUN ? "DRY RUN: pump disabled" : "LIVE: pump enabled");
}

void loop() {
  int m = readMoisturePct();
  bool allowed = !everWatered || (millis() - lastSession > LOCKOUT_MS);
  if (m < START_PCT && allowed) waterSession();
  delay(CHECK_MS);
}
```

What to check:
- **Probe in air:** "Burst 1 of 4" starts, up to 4 bursts, then monitoring only.
- **Probe dipped in water during a soak:** reading rises above 55% and the session ends with no more bursts.
- **Probe in water at boot:** only readings, never "Burst".
- **Bucket test (`DRY_RUN = false`, mains on):** pump must stay **off** at idle and after RST (if it runs, it is wired to NC, so unplug immediately), run 5 s when the probe is in air, then stop.

---

## Test 7: Final sketch with Blynk

Fill in your Template ID, Template Name, Auth Token and Wi-Fi details. Start with `DRY_RUN = true`, then switch to `false` for a bucket test. Don't share your auth token or Wi-Fi password.

Blynk datastreams: V0 moisture % (Gauge), V1 pump running (LED), V2 water now (Button), V3 auto mode (Switch), V4 start % (Slider, default 30), V5 stop % (Slider, default 55), V6 status (String Label), V7 raw value (Label). Optional event: `pump_fault`.

```cpp
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
const unsigned long PUMP_BURST_MS    = 5000;      // pump on per burst
const unsigned long SOAK_MS          = 180000;    // 3 min wait after each burst
const unsigned long CHECK_MS         = 30000;     // soil check when idle
const int           MAX_BURSTS       = 4;         // per session
const unsigned long FAULT_LOCKOUT_MS = 7200000;   // 2 h if target never reached

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
        if (m >= stopPct)                  endSession(false);
        else if (burstCount >= MAX_BURSTS) endSession(true);
        else                               startPump();
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
```

Tests for the Blynk version:
1. `DRY_RUN = true`, pump unplugged: confirm readings in Serial and on the gauge, and that the status label updates.
2. Test the **Water now** button and the sliders.
3. `DRY_RUN = false`, pump in a bucket: **Water now** should run the pump for exactly 5 s.
4. Put the probe in real soil, record dry and wet percentages, and set the Start and Stop sliders from them.

The Blynk library has not been verified on the ESP32-C5 core. If it fails to compile, paste the error.

---

## Safety reminders

- Unplug mains and USB before touching any wiring.
- The pump must be wired to **NO**, with NC empty. It must stay **off** at idle and after reset.
- Never run the pump dry, and keep the relay module in a closed box away from water.
- Add a 1A/2A fuse and an RCD/GFCI socket before leaving it unattended.
