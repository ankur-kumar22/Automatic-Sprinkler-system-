# Automatic Plant Watering System (ESP32-C5 + Blynk)

An automatic plant watering system built on the **ESP32-C5**, a **resistive soil moisture sensor** (LM393 module), a **5V optocoupler relay module** and a **220-240V AC submersible pump**. It waters in short bursts, waits for the water to soak in, re-checks the soil, and can be monitored and controlled from the **Blynk** phone app.

> **WARNING: this project switches mains voltage.** Mains can kill. If you have not wired mains before, have an experienced person check your work before you plug anything in. See [Safety](#safety).

## Project photos

These photos show the physical project setup and the ESP32-C5 serial output during testing.

| Project setup | Close-up of the hardware and serial monitor |
|---|---|
| ![Full project setup](images/project-setup.jpg) | ![Project hardware and serial monitor close-up](images/project-setup-closeup.jpg) |

---

## Features

- Reads soil moisture and converts it to a 0-100% value (calibrated for this sensor)
- Waters in **short bursts** with a **soak delay** so the pot is never flooded
- **Non-blocking state machine** (IDLE, PUMPING, SOAKING): Blynk stays connected, and watering keeps working if Wi-Fi drops
- Safety limits: max bursts per session, fault lockout, pump off at boot
- Blynk dashboard: moisture gauge, pump LED, "Water now" button, auto mode switch, start/stop sliders, status text
- `DRY_RUN` mode to test all the logic without ever switching the relay

## Hardware

| Part | Notes |
|---|---|
| ESP32-C5 board (N16R8) | Needs ESP32 Arduino core 3.3.0 or newer |
| Resistive soil moisture sensor | Probe + blue LM393 module (MH-Sensor-Series), pins VCC, GND, DO, AO |
| 1-channel relay module | 5V, optocoupler, **active-low**, JQC-3FC (NT73) relay, 10A/250VAC contacts |
| Submersible pump | 220-240V AC, 50Hz, 20W, about 0.1A, no earth wire |
| Mains extension cord | Rated 6A / 240V |
| Fuse | **1A or 2A** inline/plug fuse (recommended before unattended use) |
| RCD/GFCI socket | Strongly recommended, since the pump has no earth and sits in water |
| Plastic enclosure | Closed box for the relay module |
| Jumper wires, USB cable | USB powers the ESP32 separately from the mains circuit |

## Wiring

### Low-voltage side

| Device pin | ESP32-C5 pin |
|---|---|
| Sensor VCC | 3V3 |
| Sensor GND | GND |
| Sensor AO | **GPIO6** (ADC1_CH5) |
| Sensor DO | Not used |
| Relay VCC | **5V** (the coil needs about 72 mA at 5V; 3V3 is not enough) |
| Relay GND | GND |
| Relay IN | **GPIO24** |

Notes:
- GPIO6 is correct for the **ESP32-C5-DevKitC-1**. On the ESP-SensairShuttle board GPIO6 is the microphone ADC input, so use GPIO4 there.
- The yellow jumper on the relay module should bridge **VR and VCC**.
- The blue potentiometer on the sensor only affects the DO output, so it is ignored.

### Mains side (screw terminal on the relay)

Terminal order, with the relay upright: **top = NO, middle = COM, bottom = NC.** Mark them with tape. Verify with a multimeter or the LED test before connecting mains.

```
Extension LIVE ──► [1A fuse] ──► COM (middle)
                                 NO  (top)    ──► Pump LIVE
Extension NEUTRAL ───────────────────────────► Pump NEUTRAL (straight through)
NC: leave empty
```

- Switch **only the live wire**. Neutral never goes through the relay.
- The pump must be on **NO**, so it runs only when the relay is on.

## Safety

1. **Unplug everything** (mains and USB) before touching any wire.
2. Put the relay module in a **closed plastic box**, higher than the tank and plant, with no touchable bare metal.
3. Leave no copper strands exposed, and tighten the screws firmly.
4. Make a drip loop in the pump cable.
5. Power the ESP32 from a **separate USB supply**.
6. **Never run the pump dry.** Keep the tank filled, and keep the pump submerged as its label requires.
7. Do not touch the water or wires while the mains is plugged in.
8. Use a 1A/2A fuse and an RCD/GFCI socket before leaving the system unattended.
9. First test: the pump must stay **off** when the relay is idle and after a reset. If it runs, unplug immediately, since it is wired to NC.

## Relay behavior (important)

The relay module is **active-low** and its opto LED is powered from 5V, while the ESP32 outputs only 3.3V. Driving IN HIGH leaves the LED dim and the relay half-energized. The fix used in the code:

```cpp
void relayOn()  { pinMode(RELAY_PIN, OUTPUT); digitalWrite(RELAY_PIN, LOW); }  // ON
void relayOff() { pinMode(RELAY_PIN, INPUT); }                                  // OFF (released)
```

`relayOff()` is also called first thing in `setup()`, so the pump stays off at boot.

## Sensor calibration

Resistive sensor: **higher reading = drier**.

| Condition | Raw ADC value |
|---|---|
| Probe in air (dry) | about 3350 |
| Probe in water (wet) | about 1300 |

```cpp
const int DRY_VALUE = 3350;
const int WET_VALUE = 1300;
```

Your own sensor may differ, so recalibrate: hold the probe in air and in water (tip only, keep the blue module dry), wait for the value to settle, and note it. Then check real soil, since soil readings are usually more useful than air and water values, and set the start/stop percentages from them.

## Watering logic

| Setting | Default | Purpose |
|---|---|---|
| `PUMP_BURST_MS` | 5 s | Pump time per burst |
| `SOAK_MS` | 3 min | Wait after each burst before measuring |
| `CHECK_MS` | 30 s | Soil check interval when idle |
| `MAX_BURSTS` | 4 | Maximum bursts per session |
| `FAULT_LOCKOUT_MS` | 2 h | Lockout if the target is not reached after all bursts |
| Start below | 30% | Begin watering (Blynk V4) |
| Stop at | 55% | Stop watering (Blynk V5) |

Flow: soil below start % → pump burst → soak → re-measure → repeat until the stop % is reached or `MAX_BURSTS` is hit. If the target is never reached, the system locks out for 2 hours and sends a `pump_fault` event, which usually means an empty tank, a dead pump or a probe that fell out.

Tuning: run one burst into a measuring cup. If one burst is more than about a fifth of what the pot needs, shorten it.

## Blynk setup

1. Create a template at [blynk.cloud](https://blynk.cloud) and note the **Template ID** and **Template Name**.
2. Add these datastreams:

| Pin | Type | Use | Widget |
|---|---|---|---|
| V0 | Integer 0-100 | Moisture % | Gauge |
| V1 | Integer 0-1 | Pump running | LED |
| V2 | Integer 0-1 | Water now (one burst) | Button |
| V3 | Integer 0-1 | Auto mode | Switch |
| V4 | Integer 0-100 (default 30) | Start below % | Slider |
| V5 | Integer 0-100 (default 55) | Stop at % | Slider |
| V6 | String | Status | Label |
| V7 | Integer 0-4095 | Raw sensor value | Label |

3. Optional: create an event named `pump_fault` for phone notifications.
4. Create a device from the template and copy its **Auth Token**. Do not share it or commit it to a public repository.
5. In the Arduino IDE, install the **Blynk** library (by Volodymyr Shymanskyy).

## Software setup

1. Install the Arduino IDE and the **ESP32 Arduino core 3.3.0 or newer** (needed for ESP32-C5).
2. Select the **ESP32C5 Dev Module** board. Enable **USB CDC On Boot** if the Serial Monitor shows nothing.
3. Fill in the credentials at the top of the sketch:

```cpp
#define BLYNK_TEMPLATE_ID   "TMPLxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "Plant Waterer"
#define BLYNK_AUTH_TOKEN    "YOUR_AUTH_TOKEN"
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
```

4. Upload, and open the Serial Monitor at **115200 baud**.

> The Blynk library has not been verified on the ESP32-C5 core. If you get a compile error, open an issue or ask for a fix.

## Testing procedure

For fast testing, temporarily shorten the timings (for example `SOAK_MS = 10000`, `CHECK_MS = 5000`, and a short lockout), then restore them.

| Stage | Setup | Expected result |
|---|---|---|
| 1. Logic | `DRY_RUN = true`, mains unplugged, pump disconnected | Serial shows bursts and readings, and the relay **never clicks** |
| 2. Relay | `DRY_RUN = false`, mains unplugged | Two clean clicks per burst, LED fully on then fully off, silent at boot and idle |
| 3. Bucket | Pump submerged in a bucket, mains plugged in | Pump **off** at idle and after RST, runs 5 s when the probe is in air, stops when the probe goes in water |
| 4. Plant | Real timings restored | Record dry and wet soil readings, set start/stop %, watch the first full cycle |

Checks on the Serial output:
- Probe in air → reading near 0% → "Burst" starts.
- Probe in water during the soak → reading rises above the stop % → session ends with no further bursts.
- Probe in water at boot → only readings are printed and no burst starts.

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| Sensor readings noisy (min/max swing by hundreds) | Loose jumper wire or wrong pin. Re-seat AO, confirm the code pin matches the wiring (GPIO6). A direct 3V3 or GND touch on the pin should read about 4095 or 0. |
| Air and water readings nearly identical | AO not connected to the ADC pin, or the probe cable is loose |
| Relay LED bright/dim and relay doesn't release | Active-low module driven HIGH at 3.3V. Use the `relayOff()` pin-release method above. |
| Relay doesn't click | Relay VCC must be on 5V, and check the GND and VR/VCC jumper |
| Pump runs while the relay is idle | Pump is wired to NC. Unplug, and move it to NO. |
| Boot message `MSPI Timing: Failed to allocate dummy cacheline...` | Harmless early-boot message on PSRAM boards, so ignore it |
| Nothing in Serial Monitor | Set baud to 115200, enable USB CDC On Boot, press RST |
| Moisture reads 8-13% in air | Probe still damp from a water test, so let it dry |

## Known limitations and improvements

- **Resistive probe corrosion:** a constantly powered resistive probe corrodes within weeks or months. A capacitive soil sensor (v1.2) lasts far longer, or power the probe from a GPIO or transistor only while measuring.
- No tank-level sensor. Add a float switch to prevent dry running.
- Possible upgrades: deep sleep, MQTT/Home Assistant, a schedule (no watering at night), and a pump-run-time log.

## Disclaimer

This project is provided as-is for learning and hobby use. Working with mains voltage is dangerous, and you are responsible for correct and safe wiring. The author accepts no liability for damage or injury.
