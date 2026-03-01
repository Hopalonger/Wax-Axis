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
  if (v < 100) v = 100;
  if (v > 3000) v = 3000;
  return v;
}

static inline int waxForwardSpeedUnits(WaxRunKind kind){
  int base = (int)gSettings.routineSpeedUnits;
  if (kind == WAX_KIND_PREHEAT) {
    return clampSpeedUnits((int)lroundf(base * gSettings.preheatMult));
  }
  return clampSpeedUnits(base);
}

static inline int waxReturnSpeedUnits(){
  int base = (int)gSettings.routineSpeedUnits;
  float mult = (float)gSettings.returnSpeedPct / 100.0f;
  return clampSpeedUnits((int)lroundf(base * mult));
}

static inline void waxStopInternal(const String& why){
  relaySet(false);
  motionStopAll();

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

  // Ensure we're at the start position first (heater OFF)
  relaySet(false);
  motionGoto(gStart);
  gWaxState = WAX_GOTO_START;
  gMsg = "going to start";
  return true;
}

static inline bool waxStartPreheat(){ return waxStart(WAX_KIND_PREHEAT, 1); }
static inline bool waxStartRun(uint32_t passes){ return waxStart(WAX_KIND_RUN, passes); }

static inline void waxReturnToParkNow(){
  if (!motionCanAcceptCommand()) { waxStopInternal("Power Bad / Disabled"); return; }
  if (!homingIsDone()) { gMsg = "Run homing first"; return; }
  relaySet(false);
  motionGoto(homingParkPos());
  gMsg = "returning to park";
}

static inline void waxStop(){ waxStopInternal("stopped"); }

static inline void waxTask(){
  if (gWaxState == WAX_IDLE || gWaxState == WAX_DONE || gWaxState == WAX_ERROR) return;

  if (!motionCanAcceptCommand()) {
    waxStopInternal("Power Bad / Disabled");
    return;
  }

  switch(gWaxState){
    case WAX_GOTO_START: {
      if (motionGotoIsReached()) {
        // Heater ON + delay
        relaySet(true);
        gT0 = millis();
        gWaxState = WAX_HEATER_ON_DELAY;
        gMsg = "heater on";
      }
    } break;

    case WAX_HEATER_ON_DELAY: {
      if ((uint32_t)(millis() - gT0) >= (uint32_t)gSettings.heaterDelayMs) {
        // Move out (heater ON)
        int fwd = waxForwardSpeedUnits(gWaxKind);
        // Temporarily set routine speed high by just using motionGoto's proportional
        // controller target; motion.h clamps based on gSettings.routineSpeedUnits,
        // so we bump the base speed during the outward leg:
        long saved = gSettings.routineSpeedUnits;
        gSettings.routineSpeedUnits = fwd;
        motionGoto(gEnd);
        gSettings.routineSpeedUnits = saved;

        gWaxState = WAX_MOVE_OUT;
        gMsg = "moving out";
      }
    } break;

    case WAX_MOVE_OUT: {
      if (motionGotoIsReached()) {
        relaySet(false);
        gT0 = millis();
        gWaxState = WAX_HEATER_OFF;
        gMsg = "heater off";
      }
    } break;

    case WAX_HEATER_OFF: {
      if (millis() - gT0 >= 50) {
        // Return to park (heater OFF)
        int ret = waxReturnSpeedUnits();
        long saved = gSettings.routineSpeedUnits;
        gSettings.routineSpeedUnits = ret;
        motionGoto(gStart);
        gSettings.routineSpeedUnits = saved;

        gWaxState = WAX_RETURN_PARK;
        gMsg = "returning";
      }
    } break;

    case WAX_RETURN_PARK: {
      if (motionGotoIsReached()) {
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