#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct WaxAxisSettings {
  // Driver / low-level
  bool   driverEnabled = true;
  String setVoltage    = "5";     // default to 5V so you can run from laptop USB power
  String microsteps    = "32";
  String current       = "10";
  String stallThreshold= "63";
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

  // Operating speed (UI units, 0..400)
  long routineSpeedUnits = 200;

  // Wax routine
  float preheatMult     = 2.0f;
  int   returnSpeedPct  = 200;
  int   heaterDelayMs   = 500;
};

static Preferences gPrefs;
static WaxAxisSettings gSettings;

// Increment this when you change preference semantics/defaults.
static const uint32_t SETTINGS_SCHEMA_VER = 2;

static inline void settingsMigrateIfNeeded() {
  gPrefs.begin("settings", false);

  uint32_t ver = gPrefs.getUInt("schema", 0);
  if (ver < SETTINGS_SCHEMA_VER) {
    // ---- Migration rules ----
    // v2: default PD voltage should be 5V (previous builds often left 20V in NVS).
    // Only force it on migration so users can still change it later.
    gPrefs.putString("voltage", "5");

    // v2: ensure standstill strings are consistent with UI options
    // (UI uses FREEWHEELING; driver mapping accepts both, but keep it neat).
    String ssm = gPrefs.getString("standstillMode", "NORMAL");
    ssm.trim(); ssm.toUpperCase();
    if (ssm == "FREEWHEEL") ssm = "FREEWHEELING";
    gPrefs.putString("standstillMode", ssm);

    gPrefs.putUInt("schema", SETTINGS_SCHEMA_VER);
  }

  // First-run init (keep separate from schema so we can migrate later)
  bool inited = gPrefs.getBool("init", false);
  if (!inited) {
    gPrefs.putBool("init", true);

    gPrefs.putUInt("schema", SETTINGS_SCHEMA_VER);

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
  settingsMigrateIfNeeded();
  gPrefs.begin("settings", false);

  gSettings.driverEnabled      = gPrefs.getBool("driverEnabled", true);
  gSettings.setVoltage         = gPrefs.getString("voltage", "5");
  gSettings.microsteps         = gPrefs.getString("microsteps", "32");
  gSettings.current            = gPrefs.getString("current", "10");
  gSettings.stallThreshold     = gPrefs.getString("stallThreshold", "63");
  gSettings.standstillMode     = gPrefs.getString("standstillMode", "NORMAL");

  gSettings.gotoTolCounts      = gPrefs.getLong("gotoTol", 3);
  gSettings.ppTolCounts        = gPrefs.getLong("ppTol", 6);

  gSettings.homingBackoffCounts= gPrefs.getLong("homeBackoff", 200);
  gSettings.homingTimeoutMs    = gPrefs.getUInt("homeTimeout", 120000);
  gSettings.homeSideIsMin      = gPrefs.getBool("homeIsMin", true);

  gSettings.edgeKeepoffCounts  = gPrefs.getLong("edgeKeepoff", 800);
  gSettings.parkOffsetCounts   = gPrefs.getLong("parkOffset", 1000);

  gSettings.routineSpeedUnits  = gPrefs.getLong("routineSpeed", 200);

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

  if (gSettings.routineSpeedUnits < 50)  gSettings.routineSpeedUnits = 50;
  if (gSettings.routineSpeedUnits > 400) gSettings.routineSpeedUnits = 400;

  int currentPct = gSettings.current.toInt();
  if (currentPct < 5) currentPct = 5;
  if (currentPct > 100) currentPct = 100;
  gSettings.current = String(currentPct);

  int stall = gSettings.stallThreshold.toInt();
  if (stall < -64) stall = -64;
  if (stall > 63) stall = 63;
  gSettings.stallThreshold = String(stall);

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
