#pragma once
#include <Arduino.h>

#include "settings.h"
#include "motion.h"
#include "relay_io.h"
#include "homing.h"

enum WaxRunKind : uint8_t { WAX_KIND_NONE=0, WAX_KIND_PREHEAT=1, WAX_KIND_RUN=2 };

enum WaxState : uint8_t {
  WAX_IDLE=0,
  WAX_GOTO_START,
  WAX_HEATER_ON_DELAY,
  WAX_MOVE_OUT,
  WAX_HEATER_OFF,
  WAX_RETURN_PARK,
  WAX_DONE,
  WAX_ERROR
};

static WaxRunKind gWaxKind  = WAX_KIND_NONE;
static WaxState   gWaxState = WAX_IDLE;

static long gStart = 0;
static long gEnd   = 0;

static uint32_t gPassesReq = 0;
static uint32_t gPassIndex = 0;

static uint32_t gT0 = 0;
static String   gMsg = "idle";

static const long WAX_START_SKIP_WINDOW_COUNTS = 20;

// ---- Speed override handling (THE IMPORTANT FIX) ----
static bool gSpeedOverrideActive = false;
static long gSavedRoutineSpeedUnits = 0;

static inline void speedOverrideBegin(long newUnits){
  if (!gSpeedOverrideActive) {
    gSavedRoutineSpeedUnits = gSettings.routineSpeedUnits;
    gSpeedOverrideActive = true;
  }
  gSettings.routineSpeedUnits = newUnits;
}

static inline void speedOverrideEnd(){
  if (gSpeedOverrideActive) {
    gSettings.routineSpeedUnits = gSavedRoutineSpeedUnits;
    gSpeedOverrideActive = false;
  }
}

static inline const char* waxStateName(WaxState s){
  switch(s){
    case WAX_IDLE: return "Idle";
    case WAX_GOTO_START: return "Go To Start";
    case WAX_HEATER_ON_DELAY: return "Heater Delay";
    case WAX_MOVE_OUT: return "Moving Out";
    case WAX_HEATER_OFF: return "Heater Off";
    case WAX_RETURN_PARK: return "Returning";
    case WAX_DONE: return "Done";
    case WAX_ERROR: return "Error";
    default: return "?";
  }
}

static inline int clampSpeedUnits(int v){
  if (v < 0) v = 0;
  if (v > 400) v = 400;
  return v;
}

// Forward speed behavior:
// - RUN: base operating speed
// - PREHEAT: base * preheatMult  (so if preheatMult=2.0 => preheat is 2x faster)
static inline int waxForwardSpeedUnits(WaxRunKind kind){
  int base = (int)gSettings.routineSpeedUnits;
  if (kind == WAX_KIND_PREHEAT) {
    return clampSpeedUnits((int)lroundf((float)base * (float)gSettings.preheatMult));
  }
  return clampSpeedUnits(base);
}

static inline int waxReturnSpeedUnits(){
  int base = (int)gSettings.routineSpeedUnits;
  float mult = (float)gSettings.returnSpeedPct / 100.0f;
  return clampSpeedUnits((int)lroundf((float)base * mult));
}

static inline void waxStopInternal(const String& why){
  relaySet(false);
  motionStopAll();
  speedOverrideEnd();

  gWaxKind = WAX_KIND_NONE;
  gWaxState = WAX_IDLE;
  gPassesReq = 0;
  gPassIndex = 0;
  gMsg = why;
}

static inline bool waxBusy(){
  return gWaxState != WAX_IDLE && gWaxState != WAX_DONE;
}

static inline bool waxStart(WaxRunKind kind, uint32_t passes){
  if (waxBusy()) return false;
  if (!motionCanAcceptCommand()) { gMsg = "Power Bad / Disabled"; return false; }
  if (!relayIsReady()) { gMsg = "Heater I2C missing"; return false; }
  if (!homingIsDone()) { gMsg = "Run homing first"; return false; }

  gStart = homingParkPos();
  gEnd   = homingOpEnd();

  gWaxKind = kind;
  gPassesReq = (kind == WAX_KIND_PREHEAT) ? 1 : (passes < 1 ? 1 : passes);
  gPassIndex = 0;

  relaySet(false);
  speedOverrideEnd(); // make sure no stale override

  encoderReadNow();
  long posNow = encoderGetCounts();
  if (labs_long(gStart - posNow) <= WAX_START_SKIP_WINDOW_COUNTS) {
    relaySet(true);
    gT0 = millis();
    gWaxState = WAX_HEATER_ON_DELAY;
    gMsg = "at start, heater on";
  } else {
    motionGoto(gStart);
    gWaxState = WAX_GOTO_START;
    gMsg = "going to start";
  }
  return true;
}

static inline bool waxStartPreheat(){ return waxStart(WAX_KIND_PREHEAT, 1); }
static inline bool waxStartRun(uint32_t passes){ return waxStart(WAX_KIND_RUN, passes); }

static inline void waxReturnToParkNow(){
  if (!motionCanAcceptCommand()) { waxStopInternal("Power Bad / Disabled"); return; }
  if (!homingIsDone()) { gMsg = "Run homing first"; return; }
  relaySet(false);
  speedOverrideEnd();
  motionGoto(homingParkPos());
  gMsg = "returning to park";
}

static inline void waxStop(){ waxStopInternal("stopped"); }

static inline void waxReset(){
  waxStopInternal("reset");
}

static inline void waxTask(){
  if (gWaxState == WAX_IDLE || gWaxState == WAX_DONE || gWaxState == WAX_ERROR) return;

  if (!motionCanAcceptCommand()) {
    waxStopInternal("Power Bad / Disabled");
    return;
  }

  switch(gWaxState){
    case WAX_GOTO_START: {
      if (motionGotoIsReached()) {
        relaySet(true);
        gT0 = millis();
        gWaxState = WAX_HEATER_ON_DELAY;
        gMsg = "heater on";
      }
    } break;

    case WAX_HEATER_ON_DELAY: {
      if ((uint32_t)(millis() - gT0) >= (uint32_t)gSettings.heaterDelayMs) {
        // ---- OUTWARD LEG ----
        // IMPORTANT: Keep speed override active until we actually reach the end.
        int fwd = waxForwardSpeedUnits(gWaxKind);
        speedOverrideBegin(fwd);

        motionGoto(gEnd);

        gWaxState = WAX_MOVE_OUT;
        gMsg = (gWaxKind == WAX_KIND_PREHEAT) ? "preheat moving out" : "moving out";
      }
    } break;

    case WAX_MOVE_OUT: {
      if (motionGotoIsReached()) {
        // We’re done with outward leg speed override now
        speedOverrideEnd();

        relaySet(false);
        gT0 = millis();
        gWaxState = WAX_HEATER_OFF;
        gMsg = "heater off";
      }
    } break;

    case WAX_HEATER_OFF: {
      if (millis() - gT0 >= 50) {
        // ---- RETURN LEG ----
        int ret = waxReturnSpeedUnits();
        speedOverrideBegin(ret);

        motionGoto(gStart);

        gWaxState = WAX_RETURN_PARK;
        gMsg = "returning";
      }
    } break;

    case WAX_RETURN_PARK: {
      if (motionGotoIsReached()) {
        // Done with return speed override
        speedOverrideEnd();

        gPassIndex++;
        if (gPassIndex >= gPassesReq) {
          gWaxState = WAX_DONE;
          gMsg = "done";
        } else {
          relaySet(true);
          gT0 = millis();
          gWaxState = WAX_HEATER_ON_DELAY;
          gMsg = "next pass";
        }
      }
    } break;

    default: break;
  }
}

static inline String waxStatusJson(){
  String json = "{";
  json += "\"state\":\""; json += waxStateName(gWaxState); json += "\",";
  json += "\"msg\":\""; json += gMsg; json += "\",";
  json += "\"heaterReady\":" + String(relayIsReady() ? "true" : "false") + ",";
  json += "\"heaterOn\":" + String(relayIsOn() ? "true" : "false") + ",";
  json += "\"opSpeedUnits\":" + String(gSettings.routineSpeedUnits) + ",";
  json += "\"passIndex\":" + String(gPassIndex) + ",";
  json += "\"passesReq\":" + String(gPassesReq) + ",";
  json += "\"homed\":" + String(homingIsDone() ? "true" : "false");
  json += "}";
  return json;
}
