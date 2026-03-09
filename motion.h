#pragma once
#include <Arduino.h>
#include <TMC2209.h>

#include "pins.h"
#include "settings.h"
#include "encoder_as5600.h"

// Provided by main .ino
extern TMC2209 stepper_driver;

static int  g_lastAppliedUnits = 0;

// ===== Utilities =====
static inline long labs_long(long v) { return (v < 0) ? -v : v; }

static inline int microstepsToInt(const String &ms) {
  int v = ms.toInt();
  if (v < 1) v = 1;
  if (v > 256) v = 256;
  return v;
}

// IMPORTANT: library uses FREEWHEELING (not FREEWHEEL)
static inline TMC2209::StandstillMode standstillModeFromString(String mode) {
  mode.trim();
  mode.toUpperCase();
  if (mode == "FREEWHEELING")   return TMC2209::FREEWHEELING;
  if (mode == "FREEWHEEL")      return TMC2209::FREEWHEELING;  // accept UI shorthand
  if (mode == "BRAKING")        return TMC2209::BRAKING;
  if (mode == "STRONG_BRAKING") return TMC2209::STRONG_BRAKING;
  return TMC2209::NORMAL;
}

// Convert UI "speed units" into driver velocity.
// Keep this consistent with your existing behavior.
static inline int32_t unitsToDriverVel(int units) {
  int ms = microstepsToInt(gSettings.microsteps);
  const float k = 4.0f; // bump to 8.0f if manual velocity feels too weak
  return (int32_t)lroundf((float)units * (float)ms * k);
}

static inline void applyVelocityUnits(int units) {
  g_lastAppliedUnits = units;
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
static int  g_posControlPolarity = 1; // +1 normal, -1 reversed
static long g_lastCtrlPos = 0;
static bool g_lastCtrlPosValid = false;
static uint8_t g_divergeTicks = 0;

// Jog sizes (encoder counts)
static const long JOG_SMALL = 1024;
static const long JOG_LARGE = 4096;

static uint32_t g_lastCtrlMs = 0;
static const uint32_t CTRL_PERIOD_MS = 10;

// ===== Driver init / setup =====
static inline void motionBegin() {
  pinMode(TMC_EN, OUTPUT);
  digitalWrite(TMC_EN, LOW);

  stepper_driver.setRunCurrent((uint8_t)gSettings.current.toInt());
  stepper_driver.setMicrostepsPerStep(microstepsToInt(gSettings.microsteps));
  stepper_driver.setStallGuardThreshold(gSettings.stallThreshold.toInt());
  stepper_driver.setStandstillMode(standstillModeFromString(gSettings.standstillMode));

  stepper_driver.enable();
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

static inline bool motionStallDetected() {
  const int MIN_CMD_UNITS = 15;
  const uint32_t STALL_WINDOW_MS = 150;
  const long STALL_MIN_DELTA_COUNTS = 3;

  static bool active = false;
  static uint32_t startMs = 0;
  static long startPos = 0;

  bool diag = (digitalRead(DIAG) == HIGH);
  bool commandingMotion = (abs(g_lastAppliedUnits) >= MIN_CMD_UNITS);
  if (!diag || !commandingMotion) {
    active = false;
    return false;
  }

  encoderReadNow();
  long posNow = encoderGetCounts();
  uint32_t now = millis();
  if (!active) {
    active = true;
    startMs = now;
    startPos = posNow;
    return false;
  }
  if ((uint32_t)(now - startMs) < STALL_WINDOW_MS) return false;

  long traveled = labs_long(posNow - startPos);
  active = false;
  return (traveled <= STALL_MIN_DELTA_COUNTS);
}

static inline void motionStopAll() {
  g_sliderCmd = 0;
  g_gotoActive = false;
  g_gotoReached = false;
  applyVelocityUnits(0);
  g_gotoMsg = "Stopped";
  g_divergeTicks = 0;
  g_lastCtrlPosValid = false;
}

static inline void motionGoto(long target) {
  if (g_motionLocked) return;
  if (!motionCanAcceptCommand()) { motionStopAll(); g_gotoMsg = "Power Bad / Disabled"; return; }
  g_sliderCmd = 0;
  g_gotoTarget = target;
  g_gotoActive = true;
  g_gotoReached = false;
  g_gotoMsg = "Going";
  g_divergeTicks = 0;
  g_lastCtrlPosValid = false;
}

static inline bool motionGotoIsReached() { return g_gotoReached; }
static inline bool motionGotoIsActive() { return g_gotoActive; }

// Manual velocity slider
static inline void motionSetSlider(int slider){
  if (g_motionLocked) return;
  if (!motionCanAcceptCommand()) { motionStopAll(); g_gotoMsg = "Power Bad / Disabled"; return; }
  if (slider > 3000) slider = 3000;
  if (slider < -3000) slider = -3000;
  g_gotoActive = false;
  g_gotoReached = false;
  g_sliderCmd = slider;
  g_gotoMsg = (slider == 0) ? "Idle" : "Velocity";
}

static inline void motionJog(int which){
  if (g_motionLocked) return;
  if (!motionCanAcceptCommand()) { motionStopAll(); g_gotoMsg = "Power Bad / Disabled"; return; }

  encoderReadNow();
  long posNow = encoderGetCounts();
  long target = posNow;

  if (which == 1)      target = posNow - JOG_LARGE;
  else if (which == 2) target = posNow - JOG_SMALL;
  else if (which == 3) target = posNow + JOG_SMALL;
  else if (which == 4) target = posNow + JOG_LARGE;
  else return;

  g_sliderCmd = 0;
  motionGoto(target);
  g_gotoMsg = "Jog";
}

static inline String motionGotoStatus(){
  if (!gSettings.driverEnabled) return "Driver Disabled";
  if (digitalRead(PG) != LOW)   return "Power Bad";
  if (g_motionLocked)           return "Locked";
  if (g_gotoActive)             return "Going";
  if (g_gotoReached)            return "Reached";
  return g_gotoMsg;
}

static inline void motionSetRoutineSpeedUnits(int units){
  if (units < 5) units = 5;
  if (units > 200) units = 200;
  gSettings.routineSpeedUnits = units;
}

// Simple P controller -> velocity command
static inline bool motionGoTo(long target, long tol, uint32_t dtMs) {
  (void)dtMs;
  encoderReadNow();
  long pos = encoderGetCounts();
  long err = target - pos;
  if (labs_long(err) <= tol) {
    applyVelocityUnits(0);
    return true;
  }

  const float Kp = 0.04f;
  int v = (int)lroundf(Kp * (float)err) * g_posControlPolarity;

  int vmax = (int)gSettings.routineSpeedUnits;
  if (v >  vmax) v =  vmax;
  if (v < -vmax) v = -vmax;

  int minDrive = vmax / 8;
  if (minDrive < 3) minDrive = 3;
  if (minDrive > 30) minDrive = 30;

  if (v > 0 && v < minDrive) v = minDrive;
  if (v < 0 && v > -minDrive) v = -minDrive;

  // Auto-correct encoder/motor polarity if we repeatedly move away from target.
  if (g_lastCtrlPosValid) {
    long dPos = pos - g_lastCtrlPos;
    bool movingAway = (labs_long(err) > (tol * 2)) && ((err > 0 && dPos < 0) || (err < 0 && dPos > 0));
    if (movingAway) {
      if (g_divergeTicks < 255) g_divergeTicks++;
      if (g_divergeTicks >= 8) {
        g_posControlPolarity = -g_posControlPolarity;
        g_divergeTicks = 0;
      }
    } else {
      g_divergeTicks = 0;
    }
  }
  g_lastCtrlPos = pos;
  g_lastCtrlPosValid = true;

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
  g_lastCtrlMs = now;

  if (motionStallDetected()) {
    motionStopAll();
    g_gotoMsg = "Stall Detected";
    return;
  }

  if (g_gotoActive) {
    bool arrived = motionGoTo(g_gotoTarget, gSettings.gotoTolCounts, CTRL_PERIOD_MS);
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
