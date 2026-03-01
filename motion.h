#pragma once
#include <Arduino.h>
#include <TMC2209.h>

#include "pins.h"
#include "settings.h"
#include "encoder_as5600.h"

// Provided by main .ino
extern TMC2209 stepper_driver;

// ===== Utilities =====
static inline long labs_long(long v) { return (v < 0) ? -v : v; }

// Convert UI "speed units" (100..3000) into driver velocity.
// This matches your existing approach: scale by microsteps so UI stays consistent.
static inline int microstepsToInt(const String &ms) {
  int v = ms.toInt();
  if (v < 1) v = 1;
  if (v > 256) v = 256;
  return v;
}

static inline TMC2209::StandstillMode standstillModeFromString(String mode) {
  mode.trim();
  mode.toUpperCase();
  if (mode == "FREEWHEEL")      return TMC2209::FREEWHEEL;
  if (mode == "BRAKING")        return TMC2209::BRAKING;
  if (mode == "STRONG_BRAKING") return TMC2209::STRONG_BRAKING;
  return TMC2209::NORMAL;
}

static inline int32_t unitsToDriverVel(int units) {
  int ms = microstepsToInt(gSettings.microsteps);
  // The factor below is intentionally conservative; adjust if you want faster max.
  // Bigger factor = faster actual motor for same UI value.
  const float k = 4.0f;
  return (int32_t)lroundf((float)units * (float)ms * k);
}

static inline void applyVelocityUnits(int units) {
  stepper_driver.moveAtVelocity(unitsToDriverVel(units));
}

// ===== Internal motion state =====
static bool g_motionLocked = false;
static bool g_enabledState = false;

static int  g_sliderCmd = 0;     // -3000..3000
static long g_gotoTarget = 0;
static bool g_gotoActive = false;
static bool g_gotoReached = false;
static String g_gotoMsg = "Idle";

static uint32_t g_lastCtrlMs = 0;
static const uint32_t CTRL_PERIOD_MS = 10;

// ===== Driver init / setup =====
static inline void motionBegin() {
  pinMode(TMC_EN, OUTPUT);
  digitalWrite(TMC_EN, LOW);

  stepper_driver.setRunCurrent((uint8_t)gSettings.current.toInt());
  stepper_driver.enable();

  // Default: conservative
  stepper_driver.setMicrostepsPerStep(microstepsToInt(gSettings.microsteps));

  // Stall + diag
  stepper_driver.setStallGuardThreshold(gSettings.stallThreshold.toInt());
  stepper_driver.setStandstillMode(standstillModeFromString(gSettings.standstillMode));

  g_enabledState = true;
}

static inline void motionApplySettingsFromPrefs() {
  stepper_driver.setRunCurrent((uint8_t)gSettings.current.toInt());
  stepper_driver.setMicrostepsPerStep(microstepsToInt(gSettings.microsteps));
  stepper_driver.setStallGuardThreshold(gSettings.stallThreshold.toInt());
  stepper_driver.setStandstillMode(standstillModeFromString(gSettings.standstillMode));
}

static inline bool pgOkAndEnabledRequested() {
  if (!gSettings.driverEnabled) return false;
  return (digitalRead(PG) == LOW);
}

static inline const char* motionDriverStatusText() {
  if (!gSettings.driverEnabled) return "Disabled";
  if (digitalRead(PG) != LOW)   return "Power Bad";
  return "Ready";
}

static inline void motionSetLocked(bool locked) {
  g_motionLocked = locked;
  if (locked) {
    g_sliderCmd = 0;
    g_gotoActive = false;
    g_gotoReached = false;
    applyVelocityUnits(0);
  }
}

static inline bool motionCanAcceptCommand() {
  return (!g_motionLocked) && pgOkAndEnabledRequested();
}

static inline bool motionIsIdle() {
  return (!g_motionLocked) && !g_gotoActive && (g_sliderCmd == 0);
}

static inline void motionStopAll() {
  g_sliderCmd = 0;
  g_gotoActive = false;
  g_gotoReached = false;
  applyVelocityUnits(0);
  g_gotoMsg = "Stopped";
}

static inline void motionGoto(long target) {
  if (g_motionLocked) return;
  if (!motionCanAcceptCommand()) { motionStopAll(); g_gotoMsg = "Power Bad / Disabled"; return; }
  g_sliderCmd = 0;
  g_gotoTarget = target;
  g_gotoActive = true;
  g_gotoReached = false;
  g_gotoMsg = "Going";
}

static inline bool motionGotoIsReached() { return g_gotoReached; }
static inline long motionPositionCounts() { return encoderGetCounts(); }

// Simple P controller -> velocity command
static inline bool motionGoTo(long target, long tol, uint32_t dtMs) {
  encoderReadNow();
  long pos = encoderGetCounts();
  long err = target - pos;
  if (labs_long(err) <= tol) {
    applyVelocityUnits(0);
    return true;
  }

  // proportional to error, clamped to UI speed max
  const float Kp = 0.04f;
  int v = (int)lroundf(Kp * (float)err);
  if (v >  (int)gSettings.routineSpeedUnits) v =  (int)gSettings.routineSpeedUnits;
  if (v < -(int)gSettings.routineSpeedUnits) v = -(int)gSettings.routineSpeedUnits;

  // min movement to avoid stalling
  if (v > 0 && v < 30) v = 30;
  if (v < 0 && v > -30) v = -30;

  applyVelocityUnits(v);
  return false;
}

static inline void motionEnableTask() {
  bool want = pgOkAndEnabledRequested();
  if (want && !g_enabledState) {
    stepper_driver.enable();
    g_enabledState = true;
  } else if (!want && g_enabledState) {
    motionStopAll();
    stepper_driver.disable();
    g_enabledState = false;
  }
  digitalWrite(LED2, digitalRead(DIAG));
}

static inline void motionControlTask() {
  if (g_motionLocked) return;
  if (!g_enabledState) return;

  uint32_t now = millis();
  if (now - g_lastCtrlMs < CTRL_PERIOD_MS) return;
  uint32_t dt = now - g_lastCtrlMs;
  g_lastCtrlMs = now;

  if (g_gotoActive) {
    bool arrived = motionGoTo(g_gotoTarget, gSettings.gotoTolCounts, dt);
    if (arrived) {
      g_gotoActive = false;
      g_gotoReached = true;
      g_gotoMsg = "Reached";
      applyVelocityUnits(0);
    }
  } else {
    if (g_sliderCmd != 0) applyVelocityUnits(g_sliderCmd);
    else applyVelocityUnits(0);
  }
}