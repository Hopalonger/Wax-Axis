#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct WaxAxisSettings {
  // Driver / low-level
  bool   driverEnabled = true;
  String setVoltage    = "5";     // you wanted 5V on ESP32S3 PD stepper sketch
  String microsteps    = "32";
  String current       = "30";
  String stallThreshold= "10";
  String standstillMode= "NORMAL";

  // Motion tolerances (encoder counts)
  long gotoTolCounts   = 3;
  long ppTolCounts     = 6;

  // Homing
  long     homingBackoffCounts = 200;
  uint32_t homingTimeoutMs     = 120000;  // user-adjustable now
  bool     homeSideIsMin       = true;    // min end becomes "home"

  // Rail geometry (user-facing)
  long edgeKeepoffCounts = 800;   // how far from each physical end we operate
  long parkOffsetCounts  = 1000;  // additional offset from the HOME operating edge

  // Operating speed (UI units, 100..3000)
  long routineSpeedUnits = 600;

  // Wax routine
  float preheatMult     = 2.0f;
  int   returnSpeedPct  = 200;
  int   heaterDelayMs   = 500;
};

static Preferences gPrefs;
static WaxAxisSettings gSettings;

static inline void settingsDefaultsIfMissing() {
  gPrefs.begin("settings", false);
  bool inited = gPrefs.getBool("init", false);
  if (!inited) {
    gPrefs.putBool("init", true);

    gPrefs.putBool  ("driverEnabled", gSettings.driverEnabled);
    gPrefs.putString("voltage", gSettings.setVoltage);
    gPrefs.putString("microsteps", gSettings.microsteps);
    gPrefs.putString("current", gSettings.current);
    gPrefs.putString("stallThreshold", gSettings.stallThreshold);
    gPrefs.putString("standstillMode", gSettings.standstillMode);

    gPrefs.putLong("gotoTol", gSettings.gotoTolCounts);
    gPrefs.putLong("ppTol", gSettings.ppTolCounts);

    gPrefs.putLong("homeBackoff", gSettings.homingBackoffCounts);
    gPrefs.putUInt("homeTimeout", gSettings.homingTimeoutMs);
    gPrefs.putBool("homeIsMin", gSettings.homeSideIsMin);

    gPrefs.putLong("edgeKeepoff", gSettings.edgeKeepoffCounts);
    gPrefs.putLong("parkOffset", gSettings.parkOffsetCounts);

    gPrefs.putLong("routineSpeed", gSettings.routineSpeedUnits);

    gPrefs.putFloat("preheatMult", gSettings.preheatMult);
    gPrefs.putInt  ("returnPct", gSettings.returnSpeedPct);
    gPrefs.putInt  ("heaterDelayMs", gSettings.heaterDelayMs);
  }
  gPrefs.end();
}

static inline void settingsLoad() {
  settingsDefaultsIfMissing();
  gPrefs.begin("settings", false);

  gSettings.driverEnabled      = gPrefs.getBool("driverEnabled", true);
  gSettings.setVoltage         = gPrefs.getString("voltage", "5");
  gSettings.microsteps         = gPrefs.getString("microsteps", "32");
  gSettings.current            = gPrefs.getString("current", "30");
  gSettings.stallThreshold     = gPrefs.getString("stallThreshold", "10");
  gSettings.standstillMode     = gPrefs.getString("standstillMode", "NORMAL");

  gSettings.gotoTolCounts      = gPrefs.getLong("gotoTol", 3);
  gSettings.ppTolCounts        = gPrefs.getLong("ppTol", 6);

  gSettings.homingBackoffCounts= gPrefs.getLong("homeBackoff", 200);
  gSettings.homingTimeoutMs    = gPrefs.getUInt("homeTimeout", 120000);
  gSettings.homeSideIsMin      = gPrefs.getBool("homeIsMin", true);

  gSettings.edgeKeepoffCounts  = gPrefs.getLong("edgeKeepoff", 800);
  gSettings.parkOffsetCounts   = gPrefs.getLong("parkOffset", 1000);

  gSettings.routineSpeedUnits  = gPrefs.getLong("routineSpeed", 600);

  gSettings.preheatMult        = gPrefs.getFloat("preheatMult", 2.0f);
  gSettings.returnSpeedPct     = gPrefs.getInt("returnPct", 200);
  gSettings.heaterDelayMs      = gPrefs.getInt("heaterDelayMs", 500);

  gPrefs.end();

  // clamps
  if (gSettings.gotoTolCounts < 1) gSettings.gotoTolCounts = 1;
  if (gSettings.ppTolCounts   < 1) gSettings.ppTolCounts   = 1;

  if (gSettings.homingBackoffCounts < 20) gSettings.homingBackoffCounts = 20;
  if (gSettings.homingTimeoutMs < 10000) gSettings.homingTimeoutMs = 10000;
  if (gSettings.homingTimeoutMs > 600000) gSettings.homingTimeoutMs = 600000;

  if (gSettings.edgeKeepoffCounts < 0) gSettings.edgeKeepoffCounts = 0;
  if (gSettings.parkOffsetCounts < 0) gSettings.parkOffsetCounts = 0;

  if (gSettings.routineSpeedUnits < 100)  gSettings.routineSpeedUnits = 100;
  if (gSettings.routineSpeedUnits > 3000) gSettings.routineSpeedUnits = 3000;

  if (gSettings.preheatMult < 1.0f) gSettings.preheatMult = 1.0f;
  if (gSettings.preheatMult > 5.0f) gSettings.preheatMult = 5.0f;

  if (gSettings.returnSpeedPct < 100) gSettings.returnSpeedPct = 100;
  if (gSettings.returnSpeedPct > 400) gSettings.returnSpeedPct = 400;

  if (gSettings.heaterDelayMs < 0) gSettings.heaterDelayMs = 0;
  if (gSettings.heaterDelayMs > 5000) gSettings.heaterDelayMs = 5000;
}

static inline void settingsSave() {
  gPrefs.begin("settings", false);

  gPrefs.putBool  ("driverEnabled", gSettings.driverEnabled);
  gPrefs.putString("voltage", gSettings.setVoltage);
  gPrefs.putString("microsteps", gSettings.microsteps);
  gPrefs.putString("current", gSettings.current);
  gPrefs.putString("stallThreshold", gSettings.stallThreshold);
  gPrefs.putString("standstillMode", gSettings.standstillMode);

  gPrefs.putLong("gotoTol", gSettings.gotoTolCounts);
  gPrefs.putLong("ppTol", gSettings.ppTolCounts);

  gPrefs.putLong("homeBackoff", gSettings.homingBackoffCounts);
  gPrefs.putUInt("homeTimeout", gSettings.homingTimeoutMs);
  gPrefs.putBool("homeIsMin", gSettings.homeSideIsMin);

  gPrefs.putLong("edgeKeepoff", gSettings.edgeKeepoffCounts);
  gPrefs.putLong("parkOffset", gSettings.parkOffsetCounts);

  gPrefs.putLong("routineSpeed", gSettings.routineSpeedUnits);

  gPrefs.putFloat("preheatMult", gSettings.preheatMult);
  gPrefs.putInt  ("returnPct", gSettings.returnSpeedPct);
  gPrefs.putInt  ("heaterDelayMs", gSettings.heaterDelayMs);

  gPrefs.end();
}

// Forward-declared in homing.h (implemented there)
String settingsToJsonWithHoming();