/*
  Wax Axis V7
  - ESP32-S3 Dev Module
  - TMC2209 UART velocity mode
  - AS5600 encoder
  - Qwiic GPIO expander (PCA9534 class) drives IR heater relay (IO4, addr 0x27)
  - Web UI: Operate / Setup / Settings / Update

  Notes:
  - All user-tunable parameters live in Preferences and are editable via /settings and /setup.
  - Homing finds BOTH physical ends using stall (DIAG), then derives:
      opMin = endMin + edgeKeepoff
      opMax = endMax - edgeKeepoff
      park  = (homeSide ? opMin+parkOffset : opMax-parkOffset) clamped
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
#include "ui_setup.h"
#include "ui_settings.h"
#include "ui_update.h"

// ===================== Web Server =====================
AsyncWebServer server(80);

// ===================== TMC2209 =====================
HardwareSerial TMCSerial(1);
TMC2209 stepper_driver;

// Helper: HTML OK response
static inline void sendOk(AsyncWebServerRequest *request, const String &msg = "OK") {
  request->send(200, "text/plain; charset=utf-8", msg);
}

// Helper: get POST arg with fallback
static inline String argOr(AsyncWebServerRequest *request, const char *name, const String &fallback) {
  if (request->hasParam(name, true)) return request->getParam(name, true)->value();
  return fallback;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nWax Axis V7 boot...");

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);

  pinMode(PG, INPUT_PULLUP);
  pinMode(DIAG, INPUT_PULLUP);
  pinMode(VBUS, INPUT);

  // I2C: shared bus for AS5600 + Qwiic expander
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(10);

  // Load settings first
  settingsLoad();

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Encoder
  encoderBegin();

  // Relay expander
  bool relayOk = relayBegin();
  Serial.println(relayOk ? "Relay expander OK" : "Relay expander MISSING");
  relaySet(false);

  // TMC UART
  TMCSerial.begin(115200, SERIAL_8N1, TMC_RX, TMC_TX);
  stepper_driver.setup(TMCSerial);

  // Configure motor/driver from saved settings
  motionBegin();
  motionApplySettingsFromPrefs();

  // Homing state load (rail endpoints persisted from last successful home)
  homingLoadState();

  // Web routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", UI_OPERATE_HTML);
  });

  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", UI_SETUP_HTML);
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

  // ---- Status endpoints (simple text) ----
  server.on("/powergood", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain; charset=utf-8", (digitalRead(PG) == LOW) ? "Power Good" : "Power Bad");
  });

  server.on("/voltage", HTTP_GET, [](AsyncWebServerRequest *request) {
    int v = analogRead(VBUS);
    request->send(200, "text/plain; charset=utf-8", String(v));
  });

  server.on("/position", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain; charset=utf-8", String(encoderGetCounts()));
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain; charset=utf-8", motionDriverStatusText());
  });

  // ---- JSON endpoints ----
  server.on("/settingsjson", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json; charset=utf-8", settingsToJsonWithHoming());
  });

  server.on("/homingstatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json; charset=utf-8", homingStatusJson());
  });

  server.on("/waxstatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json; charset=utf-8", waxStatusJson());
  });

  // ---- Actions ----
  server.on("/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    waxStop();
    motionStopAll();
    relaySet(false);
    sendOk(request, "Stopped");
  });

  server.on("/heater", HTTP_POST, [](AsyncWebServerRequest *request) {
    String on = argOr(request, "on", "0");
    relaySet(on == "1");
    sendOk(request, relayIsOn() ? "Heater ON" : "Heater OFF");
  });

  server.on("/setopspeed", HTTP_POST, [](AsyncWebServerRequest *request) {
    String v = argOr(request, "routinespeed", String((long)gSettings.routineSpeedUnits));
    long rs = v.toInt();
    if (rs < 100) rs = 100;
    if (rs > 3000) rs = 3000;
    gSettings.routineSpeedUnits = rs;
    settingsSave();
    sendOk(request, "OK");
  });

  server.on("/returnhome", HTTP_POST, [](AsyncWebServerRequest *request) {
    waxReturnToParkNow();
    sendOk(request, "Returning to Park");
  });

  server.on("/homing", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool ok = homingStartAsync();
    request->send(200, "text/plain; charset=utf-8", ok ? "Homing started" : "Homing already running");
  });

  server.on("/preheat", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool ok = waxStartPreheat();
    request->send(200, "text/plain; charset=utf-8", ok ? "Preheat started" : "Preheat rejected");
  });

  server.on("/run", HTTP_POST, [](AsyncWebServerRequest *request) {
    uint32_t passes = (uint32_t)argOr(request, "passes", "2").toInt();
    if (passes < 1) passes = 1;
    if (passes > 25) passes = 25;
    bool ok = waxStartRun(passes);
    request->send(200, "text/plain; charset=utf-8", ok ? "Run started" : "Run rejected");
  });

  // Save driver setup (/setup form)
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    gSettings.driverEnabled   = request->hasParam("enabled1", true);

    gSettings.setVoltage      = argOr(request, "setvoltage", gSettings.setVoltage);
    gSettings.microsteps      = argOr(request, "microsteps", gSettings.microsteps);
    gSettings.current         = argOr(request, "current", gSettings.current);
    gSettings.stallThreshold  = argOr(request, "stall_threshold", gSettings.stallThreshold);
    gSettings.standstillMode  = argOr(request, "standstill_mode", gSettings.standstillMode);

    gSettings.homingBackoffCounts = argOr(request, "home_backoff", String(gSettings.homingBackoffCounts)).toInt();
    gSettings.gotoTolCounts       = argOr(request, "goto_tol", String(gSettings.gotoTolCounts)).toInt();

    settingsSave();
    motionApplySettingsFromPrefs();

    request->redirect("/setup");
  });

  // Save settings (/settings form)
  server.on("/savesettings", HTTP_POST, [](AsyncWebServerRequest *request) {
    gSettings.preheatMult = argOr(request, "preheat_mult", String(gSettings.preheatMult, 2)).toFloat();
    gSettings.returnSpeedPct = argOr(request, "return_pct", String(gSettings.returnSpeedPct)).toInt();
    gSettings.heaterDelayMs = argOr(request, "heater_delay", String(gSettings.heaterDelayMs)).toInt();

    gSettings.edgeKeepoffCounts = argOr(request, "edge_keepoff", String(gSettings.edgeKeepoffCounts)).toInt();
    gSettings.parkOffsetCounts  = argOr(request, "park_offset", String(gSettings.parkOffsetCounts)).toInt();
    gSettings.homingTimeoutMs   = (uint32_t)argOr(request, "homing_timeout", String(gSettings.homingTimeoutMs)).toInt();

    String hs = argOr(request, "home_side", gSettings.homeSideIsMin ? "min" : "max");
    gSettings.homeSideIsMin = (hs == "min");

    settingsSave();

    // If we already know ends, recompute park/op window immediately.
    homingRecomputeDerived();

    request->redirect("/settings");
  });

  // OTA upload handlers
  server.on(
    "/updatefw", HTTP_POST,
    [](AsyncWebServerRequest *request) { handleFwUpdateRequest(request); },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      handleFwUpload(request, filename, index, data, len, final);
    }
  );

  server.begin();
  Serial.println("Server started.");

  // Safety: stop motor + heater at boot
  waxStop();
  motionStopAll();
  relaySet(false);
}

void loop() {
  encoderTask();
  motionEnableTask();
  motionControlTask();
  homingServiceTask();   // monitors async homing completion + auto-park
  waxTask();             // wax routine state machine
}