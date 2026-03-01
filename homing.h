#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <TMC2209.h>

#include "pins.h"
#include "settings.h"
#include "encoder_as5600.h"
#include "motion.h"

// Provided by main .ino
extern TMC2209 stepper_driver;

static Preferences gHomePrefs;

// DIAG behavior (adjust if your DIAG polarity differs)
static const bool DIAG_ACTIVE_HIGH = true;
static const int  DIAG_IRQ_MODE    = RISING;

static volatile bool g_diagHit = false;

static inline void IRAM_ATTR diag_isr() {
  g_diagHit = true;
}

static inline void clearDiag() {
  g_diagHit = false;
  (void)digitalRead(DIAG);
}


struct HomingState {
  bool running = false;
  bool done    = false;
  bool error   = false;
  String msg   = "idle";

  // physical ends (encoder counts)
  long endMin  = 0;
  long endMax  = 0;

  // derived window + park
  long opMin   = 0;
  long opMax   = 0;
  long parkPos = 0;

  bool validEnds = false;
};

static HomingState gHome;
static portMUX_TYPE gHomeMux = portMUX_INITIALIZER_UNLOCKED;

// ===== Helpers =====
static inline bool diagStable(uint32_t assertMs) {
  static uint32_t sinceMs = 0;
  bool diag = (digitalRead(DIAG) == (DIAG_ACTIVE_HIGH ? HIGH : LOW));
  if (diag) {
    if (sinceMs == 0) sinceMs = millis();
    if (millis() - sinceMs >= assertMs) return true;
  } else {
    sinceMs = 0;
  }
  return false;
}

static inline int32_t rampVelToward(int32_t vNow, int32_t vTarget, int32_t dvMaxPerTick) {
  if (vTarget > vNow) {
    int32_t d = vTarget - vNow;
    if (d > dvMaxPerTick) d = dvMaxPerTick;
    return vNow + d;
  } else {
    int32_t d = vNow - vTarget;
    if (d > dvMaxPerTick) d = dvMaxPerTick;
    return vNow - d;
  }
}

static bool runUntilDiagVelRamped(int32_t vTargetUnits,
                                 uint32_t timeoutMs,
                                 uint32_t diagAssertMs,
                                 int32_t rampUnitsPerTick,
                                 uint32_t tickMs)
{
  clearDiag();
  int32_t vNow = 0;
  stepper_driver.moveAtVelocity(unitsToDriverVel(0));

  uint32_t startMs = millis();
  uint32_t lastTick = startMs;

  while (true) {
    if (g_diagHit || diagStable(diagAssertMs)) {
      stepper_driver.moveAtVelocity(unitsToDriverVel(0));
      return true;
    }
    if ((uint32_t)(millis() - startMs) >= timeoutMs) {
      stepper_driver.moveAtVelocity(unitsToDriverVel(0));
      return false;
    }
    uint32_t now = millis();
    if (now - lastTick >= tickMs) {
      lastTick = now;
      vNow = rampVelToward(vNow, vTargetUnits, rampUnitsPerTick);
      stepper_driver.moveAtVelocity(unitsToDriverVel(vNow));
    }
    vTaskDelay(1);
  }
}

static bool backoffByEncoderDeltaRamped(int32_t vTargetUnits,
                                       long backoffCounts,
                                       uint32_t timeoutMs,
                                       int32_t rampUnitsPerTick,
                                       uint32_t tickMs)
{
  encoderReadNow();
  long startPos = encoderGetCounts();
  int32_t vNow = 0;
  stepper_driver.moveAtVelocity(unitsToDriverVel(0));

  uint32_t startMs = millis();
  uint32_t lastTick = startMs;

  while (true) {
    encoderReadNow();
    long nowPos = encoderGetCounts();
    if (labs_long(nowPos - startPos) >= backoffCounts) {
      stepper_driver.moveAtVelocity(unitsToDriverVel(0));
      return true;
    }
    if ((uint32_t)(millis() - startMs) >= timeoutMs) {
      stepper_driver.moveAtVelocity(unitsToDriverVel(0));
      return false;
    }
    uint32_t now = millis();
    if (now - lastTick >= tickMs) {
      lastTick = now;
      vNow = rampVelToward(vNow, vTargetUnits, rampUnitsPerTick);
      stepper_driver.moveAtVelocity(unitsToDriverVel(vNow));
    }
    vTaskDelay(1);
  }
}

static bool homeOneSideRamped(int32_t velTowardFastUnits,
                             int32_t velTowardSlowUnits,
                             int32_t velAwayUnits,
                             long backoffCounts,
                             uint32_t settleMs,
                             uint32_t timeoutMsTotal,
                             uint32_t diagAssertMs,
                             int32_t rampUnitsPerTick,
                             uint32_t tickMs,
                             long &posAtHitOut)
{
  uint32_t tFast = timeoutMsTotal / 2;
  uint32_t tBack = timeoutMsTotal / 4;
  uint32_t tSlow = timeoutMsTotal / 4;

  if (!runUntilDiagVelRamped(velTowardFastUnits, tFast, diagAssertMs, rampUnitsPerTick, tickMs)) return false;
  vTaskDelay(pdMS_TO_TICKS(settleMs));

  if (!backoffByEncoderDeltaRamped(velAwayUnits, backoffCounts, tBack, rampUnitsPerTick, tickMs)) return false;
  vTaskDelay(pdMS_TO_TICKS(settleMs));

  if (!runUntilDiagVelRamped(velTowardSlowUnits, tSlow, diagAssertMs, rampUnitsPerTick, tickMs)) return false;
  vTaskDelay(pdMS_TO_TICKS(settleMs));

  encoderReadNow();
  posAtHitOut = encoderGetCounts();
  return true;
}

static inline void homingComputeDerivedLocked() {
  if (!gHome.validEnds) return;

  long endMin = gHome.endMin;
  long endMax = gHome.endMax;

  long keep = gSettings.edgeKeepoffCounts;
  long opMin = endMin + keep;
  long opMax = endMax - keep;
  if (opMax < opMin) {
    // keepoff too large; collapse safely
    long mid = (endMin + endMax) / 2;
    opMin = mid;
    opMax = mid;
  }

  long park;
  if (gSettings.homeSideIsMin) {
    park = opMin + gSettings.parkOffsetCounts;
    if (park > opMax) park = opMax;
  } else {
    park = opMax - gSettings.parkOffsetCounts;
    if (park < opMin) park = opMin;
  }

  gHome.opMin = opMin;
  gHome.opMax = opMax;
  gHome.parkPos = park;
}

static inline void homingSaveState() {
  if (!gHome.validEnds) return;
  gHomePrefs.begin("homing", false);
  gHomePrefs.putBool("valid", true);
  gHomePrefs.putLong("endMin", gHome.endMin);
  gHomePrefs.putLong("endMax", gHome.endMax);
  gHomePrefs.end();
}

static inline void homingLoadState() {
  gHomePrefs.begin("homing", true);
  bool valid = gHomePrefs.getBool("valid", false);
  if (valid) {
    gHome.endMin = gHomePrefs.getLong("endMin", 0);
    gHome.endMax = gHomePrefs.getLong("endMax", 0);
    gHome.validEnds = true;
    gHome.done = true;
    gHome.error = false;
    gHome.msg = "loaded";
    homingComputeDerivedLocked();
  }
  gHomePrefs.end();
}

static inline void homingRecomputeDerived() {
  portENTER_CRITICAL(&gHomeMux);
  homingComputeDerivedLocked();
  portEXIT_CRITICAL(&gHomeMux);
}

static inline bool homingIsDone() {
  bool done;
  portENTER_CRITICAL(&gHomeMux);
  done = gHome.done && gHome.validEnds && !gHome.error;
  portEXIT_CRITICAL(&gHomeMux);
  return done;
}

static inline long homingParkPos() {
  long v;
  portENTER_CRITICAL(&gHomeMux);
  v = gHome.parkPos;
  portEXIT_CRITICAL(&gHomeMux);
  return v;
}

static inline long homingOpStart() {
  // start is always park
  return homingParkPos();
}

static inline long homingOpEnd() {
  long v;
  portENTER_CRITICAL(&gHomeMux);
  // end is the far operating edge
  v = gSettings.homeSideIsMin ? gHome.opMax : gHome.opMin;
  portEXIT_CRITICAL(&gHomeMux);
  return v;
}

static inline long homingRailLengthCounts() {
  long v = 0;
  portENTER_CRITICAL(&gHomeMux);
  if (gHome.validEnds) v = labs_long(gHome.endMax - gHome.endMin);
  portEXIT_CRITICAL(&gHomeMux);
  return v;
}

// ===== Actual homing procedure =====
static float homing_function(long &outEndMin, long &outEndMax) {
  const uint32_t SETTLE_MS      = 100;
  const uint32_t DIAG_ASSERT_MS = 20;

  // Velocities in UI units (conservative)
  const int32_t VEL_FAST_UNITS = 150;
  const int32_t VEL_SLOW_UNITS = 55;
  const int32_t VEL_BACK_UNITS = 95;

  const uint32_t TICK_MS = 10;
  const int32_t  RAMP_UNITS_PER_TICK = 6;

  const uint32_t TIMEOUT_MS_TOTAL = gSettings.homingTimeoutMs;
  const long BACKOFF_COUNTS = gSettings.homingBackoffCounts;

  if (digitalRead(PG) != LOW || !gSettings.driverEnabled) return NAN;

  motionSetLocked(true);
  motionStopAll();

  stepper_driver.moveAtVelocity(unitsToDriverVel(0));
  stepper_driver.enable();

  clearDiag();
  attachInterrupt(digitalPinToInterrupt(DIAG), diag_isr, DIAG_IRQ_MODE);

  long pNeg = 0, pPos = 0;

  // First: toward + direction
  bool ok1 = homeOneSideRamped(+abs(VEL_FAST_UNITS),
                               +abs(VEL_SLOW_UNITS),
                               -abs(VEL_BACK_UNITS),
                               BACKOFF_COUNTS,
                               SETTLE_MS,
                               TIMEOUT_MS_TOTAL,
                               DIAG_ASSERT_MS,
                               RAMP_UNITS_PER_TICK,
                               TICK_MS,
                               pPos);

  if (!ok1) {
    detachInterrupt(digitalPinToInterrupt(DIAG));
    stepper_driver.moveAtVelocity(unitsToDriverVel(0));
    motionSetLocked(false);
    return NAN;
  }

  vTaskDelay(pdMS_TO_TICKS(350));

  // Second: toward - direction
  bool ok2 = homeOneSideRamped(-abs(VEL_FAST_UNITS),
                               -abs(VEL_SLOW_UNITS),
                               +abs(VEL_BACK_UNITS),
                               BACKOFF_COUNTS,
                               SETTLE_MS,
                               TIMEOUT_MS_TOTAL,
                               DIAG_ASSERT_MS,
                               RAMP_UNITS_PER_TICK,
                               TICK_MS,
                               pNeg);

  detachInterrupt(digitalPinToInterrupt(DIAG));
  stepper_driver.moveAtVelocity(unitsToDriverVel(0));
  motionSetLocked(false);

  if (!ok2) return NAN;

  outEndMin = (pNeg < pPos) ? pNeg : pPos;
  outEndMax = (pNeg < pPos) ? pPos : pNeg;
  return (float)labs_long(outEndMax - outEndMin);
}

static void homingTask(void *pv) {
  long eMin = 0, eMax = 0;
  float d = homing_function(eMin, eMax);

  portENTER_CRITICAL(&gHomeMux);
  gHome.running = false;

  if (isnan(d)) {
    gHome.error = true;
    gHome.done = false;
    gHome.validEnds = false;
    gHome.msg = "homing failed";
  } else {
    gHome.error = false;
    gHome.done = true;
    gHome.validEnds = true;
    gHome.endMin = eMin;
    gHome.endMax = eMax;
    gHome.msg = "homed";
    homingComputeDerivedLocked();
  }
  portEXIT_CRITICAL(&gHomeMux);

  if (!isnan(d)) homingSaveState();

  vTaskDelete(nullptr);
}

static inline bool homingStartAsync() {
  portENTER_CRITICAL(&gHomeMux);
  if (gHome.running) { portEXIT_CRITICAL(&gHomeMux); return false; }
  gHome.running = true;
  gHome.done = false;
  gHome.error = false;
  gHome.msg = "homing...";
  portEXIT_CRITICAL(&gHomeMux);

  xTaskCreatePinnedToCore(homingTask, "homingTask", 8192, nullptr, 1, nullptr, 1);
  return true;
}

// Auto-park after homing completes
static inline void homingServiceTask() {
  static bool wasRunning = false;

  bool running, done, err, valid;
  long park;
  portENTER_CRITICAL(&gHomeMux);
  running = gHome.running;
  done = gHome.done;
  err = gHome.error;
  valid = gHome.validEnds;
  park = gHome.parkPos;
  portEXIT_CRITICAL(&gHomeMux);

  if (running) { wasRunning = true; return; }
  if (!wasRunning) return;
  wasRunning = false;

  if (done && valid && !err) {
    // Move to park at a safe return speed (no heater)
    if (motionCanAcceptCommand()) {
      relaySet(false);
      motionGoto(park);
    }
  }
}

static inline String homingStatusJson() {
  bool running, done, err, valid;
  String msg;
  long endMin, endMax, opMin, opMax, park;

  portENTER_CRITICAL(&gHomeMux);
  running = gHome.running;
  done = gHome.done;
  err = gHome.error;
  valid = gHome.validEnds;
  msg = gHome.msg;
  endMin = gHome.endMin;
  endMax = gHome.endMax;
  opMin = gHome.opMin;
  opMax = gHome.opMax;
  park = gHome.parkPos;
  portEXIT_CRITICAL(&gHomeMux);

  String json = "{";
  json += "\"running\":" + String(running ? "true" : "false") + ",";
  json += "\"done\":" + String(done ? "true" : "false") + ",";
  json += "\"error\":" + String(err ? "true" : "false") + ",";
  json += "\"validEnds\":" + String(valid ? "true" : "false") + ",";
  json += "\"msg\":\"" + msg + "\",";
  json += "\"endMin\":" + String(endMin) + ",";
  json += "\"endMax\":" + String(endMax) + ",";
  json += "\"opMin\":" + String(opMin) + ",";
  json += "\"opMax\":" + String(opMax) + ",";
  json += "\"parkPos\":" + String(park) + ",";
  json += "\"railLength\":" + String(labs_long(endMax - endMin));
  json += "}";
  return json;
}

// settings JSON helper with homing included
inline String settingsToJsonWithHoming() {
  String json = "{";
  json += "\"enabled\":" + String(gSettings.driverEnabled ? "true" : "false") + ",";
  json += "\"voltage\":\"" + gSettings.setVoltage + "\",";
  json += "\"microsteps\":\"" + gSettings.microsteps + "\",";
  json += "\"current\":\"" + gSettings.current + "\",";
  json += "\"stallThreshold\":\"" + gSettings.stallThreshold + "\",";
  json += "\"standstillMode\":\"" + gSettings.standstillMode + "\",";
  json += "\"gotoTolCounts\":" + String(gSettings.gotoTolCounts) + ",";
  json += "\"ppTolCounts\":" + String(gSettings.ppTolCounts) + ",";
  json += "\"homingBackoffCounts\":" + String(gSettings.homingBackoffCounts) + ",";
  json += "\"homingTimeoutMs\":" + String((uint32_t)gSettings.homingTimeoutMs) + ",";
  json += "\"homeSide\":\"" + String(gSettings.homeSideIsMin ? "min" : "max") + "\",";
  json += "\"edgeKeepoffCounts\":" + String(gSettings.edgeKeepoffCounts) + ",";
  json += "\"parkOffsetCounts\":" + String(gSettings.parkOffsetCounts) + ",";

  json += "\"routineSpeedUnits\":" + String(gSettings.routineSpeedUnits) + ",";
  json += "\"preheatMult\":" + String(gSettings.preheatMult, 2) + ",";
  json += "\"returnSpeedPct\":" + String(gSettings.returnSpeedPct) + ",";
  json += "\"heaterDelayMs\":" + String(gSettings.heaterDelayMs) + ",";

  // homing-derived
  json += "\"homed\":" + String(homingIsDone() ? "true" : "false") + ",";
  json += "\"railLengthCounts\":" + String(homingRailLengthCounts()) + ",";
  json += "\"parkPos\":" + String(homingParkPos()) + ",";
  json += "\"opEnd\":" + String(homingOpEnd());

  json += "}";
  return json;
}