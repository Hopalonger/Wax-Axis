#ifndef WAX_AXIS_SKETCH_IMPL
#define WAX_AXIS_SKETCH_IMPL

/*
  Wax Axis V6 (Driver bring-up aligned to known-good V5)

  Fixes:
  - Uses settingsToJsonWithHoming() (exists in homing.h) instead of settingsToJson()
  - Uses waxStartPreheat() / waxStartRun(passes) (exists in wax_process.h)
  - PD CFG mapping + TMC UART setup identical to working V5 patterns
*/

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Preferences.h>
#include <Wire.h>
#include <TMC2209.h>

#include "pins.h"
#include "encoder_as5600.h"
#include "settings.h"
#include "relay_io.h"
#include "motion.h"
#include "homing.h"
#include "wax_process.h"
#include "ota.h"

#include "ui_style.h"
#include "ui_operate.h"
#include "ui_config.h"
#include "ui_settings.h"
#include "ui_update.h"

// ===================== Web Server =====================
AsyncWebServer server(80);

// ===================== Driver =====================
TMC2209 stepper_driver;
HardwareSerial &serial_stream = Serial2;
static const long SERIAL_BAUD_RATE = 115200;

// ===================== Voltage read helpers (MATCH V5) =====================
static float VBusVoltage = 0.0f;
static const float VREF = 3.3f;
static const float DIV_RATIO = 0.1189427313f;

static inline String argOr(AsyncWebServerRequest *request, const char *name, const String &fallback) {
  if (request->hasParam(name, true)) return request->getParam(name, true)->value();
  return fallback;
}
static inline void sendOk(AsyncWebServerRequest *request, const String &msg = "OK") {
  request->send(200, "text/plain; charset=utf-8", msg);
}

// ===================== PD CFG pin mapping (EXACT V5) =====================
static inline void configureVoltagePins() {
  // NOTE: This is NOT binary; keep exactly as V5
  if (gSettings.setVoltage == "5") {
    digitalWrite(CFG1, HIGH);
    digitalWrite(CFG2, LOW);
    digitalWrite(CFG3, LOW);
  } else if (gSettings.setVoltage == "9") {
    digitalWrite(CFG1, LOW);
    digitalWrite(CFG2, LOW);
    digitalWrite(CFG3, LOW);
  } else if (gSettings.setVoltage == "12") {
    digitalWrite(CFG1, LOW);
    digitalWrite(CFG2, LOW);
    digitalWrite(CFG3, HIGH);
  } else if (gSettings.setVoltage == "15") {
    digitalWrite(CFG1, LOW);
    digitalWrite(CFG2, HIGH);
    digitalWrite(CFG3, HIGH);
  } else {  // "20"
    digitalWrite(CFG1, LOW);
    digitalWrite(CFG2, HIGH);
    digitalWrite(CFG3, LOW);
  }
}

static inline void configureDriverFromSettings() {
  // PD output selection
  configureVoltagePins();

  // TMC params
  int ms = gSettings.microsteps.toInt();
  if (ms <= 0) ms = 1;

  stepper_driver.setRunCurrent((uint8_t)gSettings.current.toInt());
  stepper_driver.setMicrostepsPerStep(ms);
  stepper_driver.setStallGuardThreshold((int8_t)gSettings.stallThreshold.toInt());

  // Standstill mode
  if (gSettings.standstillMode == "NORMAL") stepper_driver.setStandstillMode(stepper_driver.NORMAL);
  else if (gSettings.standstillMode == "FREEWHEELING") stepper_driver.setStandstillMode(stepper_driver.FREEWHEELING);
  else if (gSettings.standstillMode == "BRAKING") stepper_driver.setStandstillMode(stepper_driver.BRAKING);
  else if (gSettings.standstillMode == "STRONG_BRAKING") stepper_driver.setStandstillMode(stepper_driver.STRONG_BRAKING);
  else stepper_driver.setStandstillMode(stepper_driver.NORMAL);

  // Motion layer depends on routine speed
  motionSetRoutineSpeedUnits((int)gSettings.routineSpeedUnits);
}

static inline String readPGState() {
  return (digitalRead(PG) == LOW) ? "Power Good" : "Power Bad";
}

static inline String readVoltage() {
  int ADCValue = analogRead(VBUS);
  VBusVoltage = ADCValue * (VREF / 4096.0f) / DIV_RATIO;
  return String(VBusVoltage, 2) + "V";
}

static inline String readEncoderPos() {
  return String(encoderGetCounts());
}

static inline String readTMCStatus() {
  if (stepper_driver.hardwareDisabled()) return "Hardware Disabled";
  TMC2209::Status st = stepper_driver.getStatus();
  if (st.over_temperature_shutdown) return "Over Temp Shutdown";
  if (st.over_temperature_warning) return "Over Temp Warning";
  return "No Errors";
}

void setup() {
  // --------- Pin bring-up (MATCH V5 ordering/levels) ---------
  pinMode(PG, INPUT);

  pinMode(CFG1, OUTPUT);
  pinMode(CFG2, OUTPUT);
  pinMode(CFG3, OUTPUT);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);

  pinMode(STEP, OUTPUT);
  pinMode(DIR, OUTPUT);
  digitalWrite(STEP, LOW);

  pinMode(MS1, OUTPUT);
  pinMode(MS2, OUTPUT);
  digitalWrite(MS1, LOW);
  digitalWrite(MS2, LOW);

  pinMode(TMC_EN, OUTPUT);
  digitalWrite(TMC_EN, LOW);  // board enable pin (V5)

  pinMode(DIAG, INPUT);
  pinMode(VBUS, INPUT);

  // ADC setup
  analogReadResolution(12);
  analogSetPinAttenuation(VBUS, ADC_11db);

  Serial.begin(115200);
  delay(200);
  Serial.println("\nWax Axis V6 (V5-aligned bring-up) boot...");

  // I2C bus
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(10);

  // Encoder + relay
  encoderBegin();
  bool relayOk = relayBegin();
  Serial.println(relayOk ? "Relay expander OK" : "Relay expander MISSING");
  relaySet(false);

  // Load settings early
  settingsLoad();

  // --------- TMC2209 UART bring-up (EXACT V5 signature) ---------
  stepper_driver.setup(serial_stream, SERIAL_BAUD_RATE, TMC2209::SERIAL_ADDRESS_0, TMC_RX, TMC_TX);
  stepper_driver.enableAutomaticCurrentScaling();
  stepper_driver.enableStealthChop();
  stepper_driver.setCoolStepDurationThreshold(5000);
  stepper_driver.disable();

  // Apply PD voltage + driver params
  configureDriverFromSettings();

  // Motion/homing init
  motionBegin();
  homingLoadState();
  waxStop();

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // --------- Pages ---------
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", UI_OPERATE_HTML);
  });
  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", UI_CONFIG_HTML);
  });
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", UI_CONFIG_HTML);
  });
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", UI_SETTINGS_HTML);
  });
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", UI_UPDATE_HTML);
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/css; charset=utf-8", UI_STYLE_CSS);
  });

  // --------- Stats ---------
  server.on("/powergood", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain; charset=utf-8", readPGState());
  });
  server.on("/voltage", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain; charset=utf-8", readVoltage());
  });
  server.on("/position", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain; charset=utf-8", readEncoderPos());
  });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain; charset=utf-8", readTMCStatus());
  });

  // IMPORTANT: V6 uses this helper (implemented in homing.h)
  server.on("/settingsjson", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json; charset=utf-8", settingsToJsonWithHoming());
  });

  server.on("/homingstatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json; charset=utf-8", homingStatusJson());
  });

  server.on("/waxstatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json; charset=utf-8", waxStatusJson());
  });

  // --------- Actions ---------
  server.on("/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    waxStop();
    motionStopAll();
    relaySet(false);
    sendOk(request, "Stopped");
  });

  server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    homingCancel();
    waxReset();
    motionStopAll();
    relaySet(false);
    sendOk(request, "Reset complete");
  });

  server.on("/heater", HTTP_POST, [](AsyncWebServerRequest *request) {
    String v = argOr(request, "heater", "");
    if (v.length() == 0) v = argOr(request, "on", "0");
    bool on = (v.toInt() != 0);
    relaySet(on);
    sendOk(request, relayIsOn() ? "Heater ON" : "Heater OFF");
  });

  server.on("/setopspeed", HTTP_POST, [](AsyncWebServerRequest *request) {
    String v = argOr(request, "routinespeed", String((long)gSettings.routineSpeedUnits));
    long rs = v.toInt();
    if (rs < 5) rs = 5;
    if (rs > 200) rs = 200;
    gSettings.routineSpeedUnits = rs;
    settingsSave();
    motionSetRoutineSpeedUnits((int)rs);
    sendOk(request, "OK");
  });

  server.on("/returnhome", HTTP_POST, [](AsyncWebServerRequest *request) {
    waxReturnToParkNow();
    sendOk(request, "Returning to Park");
  });

  server.on("/homing", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!motionCanAcceptCommand()) {
      request->send(409, "text/plain; charset=utf-8", "Power Bad / Disabled");
      return;
    }
    bool ok = homingStartAsync();
    request->send(200, "application/json; charset=utf-8", ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"already running\"}");
  });

  // FIX: real function names from wax_process.h
  server.on("/preheat", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool ok = waxStartPreheat();
    request->send(ok ? 200 : 409, "text/plain; charset=utf-8", ok ? "Preheat started" : "Preheat rejected");
  });

  // UI posts: passes=#
  server.on("/run", HTTP_POST, [](AsyncWebServerRequest *request) {
    uint32_t passes = 1;
    if (request->hasParam("passes", true)) passes = (uint32_t)request->getParam("passes", true)->value().toInt();
    if (passes < 1) passes = 1;
    if (passes > 50) passes = 50;  // safety clamp

    bool ok = waxStartRun(passes);
    request->send(ok ? 200 : 409, "text/plain; charset=utf-8", ok ? "Run started" : "Run rejected");
  });

  // Setup page save
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    gSettings.driverEnabled = request->hasParam("enabled1", true);

    gSettings.setVoltage = argOr(request, "setvoltage", gSettings.setVoltage);
    gSettings.microsteps = argOr(request, "microsteps", gSettings.microsteps);
    gSettings.current = argOr(request, "current", gSettings.current);
    gSettings.stallThreshold = argOr(request, "stall_threshold", gSettings.stallThreshold);
    gSettings.standstillMode = argOr(request, "standstill_mode", gSettings.standstillMode);

    gSettings.homingBackoffCounts = argOr(request, "home_backoff", String(gSettings.homingBackoffCounts)).toInt();
    gSettings.gotoTolCounts = argOr(request, "goto_tol", String(gSettings.gotoTolCounts)).toInt();

    settingsSave();
    configureDriverFromSettings();

    request->redirect("/setup");
  });

  // Settings page save (process parameters)
  server.on("/savesettings", HTTP_POST, [](AsyncWebServerRequest *request) {
    // UI provides percent… firmware stores multiplier
    int preheatPct = argOr(request, "preheat_pct", String((int)lroundf(gSettings.preheatMult * 100.0f))).toInt();
    if (preheatPct < 100) preheatPct = 100;
    if (preheatPct > 500) preheatPct = 500;
    gSettings.preheatMult = (float)preheatPct / 100.0f;

    gSettings.returnSpeedPct = argOr(request, "return_pct", String(gSettings.returnSpeedPct)).toInt();
    gSettings.heaterDelayMs = argOr(request, "heater_delay", String(gSettings.heaterDelayMs)).toInt();

    gSettings.edgeKeepoffCounts = argOr(request, "edge_keepoff", String(gSettings.edgeKeepoffCounts)).toInt();
    gSettings.parkOffsetCounts = argOr(request, "park_offset", String(gSettings.parkOffsetCounts)).toInt();
    gSettings.homingSpeedUnits = argOr(request, "homing_speed", String(gSettings.homingSpeedUnits)).toInt();
    gSettings.homingTimeoutMs = (uint32_t)argOr(request, "homing_timeout", String(gSettings.homingTimeoutMs)).toInt();

    String hs = argOr(request, "home_side", gSettings.homeSideIsMin ? "min" : "max");
    gSettings.homeSideIsMin = (hs == "min");

    settingsSave();

    // If we already know ends, recompute park/op window immediately.
    homingRecomputeDerived();

    request->redirect("/settings");
  });
  // OTA
  server.on("/updatefw", HTTP_POST, handleFwUpdateRequest, handleFwUpload);

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain; charset=utf-8", "Not found");
  });


  // ===================== Manual motion controls (Operate page) =====================
  // - slider: signed velocity in UI units (-400..400)
  // - positionControl: 1..4 jog buttons
  server.on("/manual", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("slider", true)) {
      int v = request->getParam("slider", true)->value().toInt();
      motionSetSlider(v);
    }
    if (request->hasParam("positionControl", true)) {
      int which = request->getParam("positionControl", true)->value().toInt();
      motionJog(which);
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/goto", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("target", true)) {
      request->send(400, "text/plain", "Missing target");
      return;
    }
    long t = request->getParam("target", true)->value().toInt();
    if (!motionCanAcceptCommand()) {
      request->send(409, "text/plain", "Power Bad / Disabled");
      return;
    }
    motionGoto(t);
    request->send(200, "text/plain", "OK");
  });

  server.on("/gotostatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", motionGotoStatus());
  });

  server.begin();

  motionStopAll();
  relaySet(false);

  digitalWrite(LED1, HIGH);
  delay(120);
  digitalWrite(LED1, LOW);

  Serial.println("Wax Axis web server started.");
}

void loop() {
  encoderTask();
  motionEnableTask();
  motionControlTask();
  homingServiceTask();
  waxTask();
}


#endif // WAX_AXIS_SKETCH_IMPL
