#pragma once
#include <Arduino.h>

#include "settings.h"
#include "motion.h"
#include "relay_io.h"
#include "homing.h"

enum WaxRunKind : uint8_t { WAX_KIND_NONE=0, WAX_KIND_PREHEAT=1, WAX_KIND_RUN=2 };

enum WaxState : uint8_t {
  WAX_IDLE=0,
  WAX_GOTO_PARK,
  WAX_HEATER_ON_DELAY,
  WAX_MOVE_OUT,
  WAX_HEATER_OFF,
  WAX_RETURN_PARK,
  WAX_DONE,
  WAX_ERROR
};

static WaxRunKind gWaxKind = WAX_KIND_NONE;
static WaxState   gWaxState = WAX_IDLE;

static long gPark = 0;
static long gOpStart = 0;
static long gOpEnd   = 0;

static uint32_t gWaxPassesReq = 0;
static uint32_t gWaxPassIndex = 0;

static uint32_t gWaxT0 = 0;
static String   gWaxMsg = "idle";

// speed bookkeeping
static int gWaxPrevVmaxUnits = 600;

static inline const char* waxStateName(WaxState s){
  switch(s){
    case WAX_IDLE: return "Idle";
    case WAX_GOTO_PARK: return "Going to Park";
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
  if (v < 50) v = 50;
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
  motionSetVmaxUnitsTemp(gWaxPrevVmaxUnits);

  gWaxKind = WAX_KIND_NONE;
  gWaxState = WAX_IDLE;
  gWaxPassesReq = 0;
  gWaxPassIndex = 0;
  gWaxMsg = why;
}

static inline bool waxBusy(){
  return gWaxState != WAX_IDLE && gWaxState != WAX_DONE;
}

static inline bool waxStart(WaxRunKind kind, uint32_t passes){
  if (waxBusy()) return false;
  if (!motionCanAcceptCommand()) { gWaxMsg = "Power Bad / Disabled"; return false; }
  if (!relayIsReady()) { gWaxMsg = "Heater I2C missing"; return false; }
  if (!homingIsDone()) { gWaxMsg = "Run homing first"; return false; }

  gPark = homingParkPos();
  gOpStart = homingOpStart();
  gOpEnd   = homingOpEnd();

  gWaxPrevVmaxUnits = motionGetVmaxUnits();

  gWaxKind = kind;
  gWaxPassesReq = (kind == WAX_KIND_PREHEAT) ? 1 : (passes < 1 ? 1 : passes);
  gWaxPassIndex = 0;

  // Ensure we are at park before doing heater stuff
  relaySet(false);
  motionSetVmaxUnitsTemp(waxReturnSpeedUnits());
  motionGoto(gPark);
  gWaxState = WAX_GOTO_PARK;
  gWaxMsg = "parking";
  return true;
}

static inline bool waxStartPreheat(){
  return waxStart(WAX_KIND_PREHEAT, 1);
}

static inline bool waxStartRun(uint32_t passes){
  return waxStart(WAX_KIND_RUN, passes);
}

static inline void waxReturnToParkNow(){
  if (!motionCanAcceptCommand()) { waxStopInternal("Power Bad / Disabled"); return; }
  if (!homingIsDone()) { gWaxMsg = "Run homing first"; return; }

  relaySet(false);
  motionSetVmaxUnitsTemp(waxReturnSpeedUnits());
  motionGoto(homingParkPos());
  gWaxMsg = "returning to park";
}

static inline void waxStop(){
  waxStopInternal("stopped");
}

static inline void waxTask(){
  if (gWaxState == WAX_IDLE || gWaxState == WAX_DONE || gWaxState == WAX_ERROR) return;

  if (!motionCanAcceptCommand()) {
    waxStopInternal("Power Bad / Disabled");
    return;
  }

  switch(gWaxState){
    case WAX_GOTO_PARK: {
      if (motionGotoIsReached()) {
        // Heater ON delay
        relaySet(true);
        gWaxT0 = millis();
        gWaxState = WAX_HEATER_ON_DELAY;
        gWaxMsg = "heater on";
      }
    } break;

    case WAX_HEATER_ON_DELAY: {
      if ((uint32_t)(millis() - gWaxT0) >= (uint32_t)gSettings.heaterDelayMs) {
        // Move out to opEnd
        motionSetVmaxUnitsTemp(waxForwardSpeedUnits(gWaxKind));
        motionGoto(gOpEnd);
        gWaxState = WAX_MOVE_OUT;
        gWaxMsg = "moving out";
      }
    } break;

    case WAX_MOVE_OUT: {
      if (motionGotoIsReached()) {
        relaySet(false);
        gWaxT0 = millis();
        gWaxState = WAX_HEATER_OFF;
        gWaxMsg = "heater off";
      }
    } break;

    case WAX_HEATER_OFF: {
      if (millis() - gWaxT0 >= 50) {
        // Return to park quickly
        motionSetVmaxUnitsTemp(waxReturnSpeedUnits());
        motionGoto(gPark);
        gWaxState = WAX_RETURN_PARK;
        gWaxMsg = "returning";
      }
    } break;

    case WAX_RETURN_PARK: {
      if (motionGotoIsReached()) {
        gWaxPassIndex++;
        if (gWaxPassIndex >= gWaxPassesReq) {
          motionSetVmaxUnitsTemp(gWaxPrevVmaxUnits);
          gWaxState = WAX_DONE;
          gWaxMsg = "done";
        } else {
          // Next pass
          relaySet(true);
          gWaxT0 = millis();
          gWaxState = WAX_HEATER_ON_DELAY;
          gWaxMsg = "next pass";
        }
      }
    } break;

    default: break;
  }
}

static inline String waxStatusJson(){
  String json = "{";
  json += "\"state\":\""; json += waxStateName(gWaxState); json += "\",";
  json += "\"msg\":\""; json += gWaxMsg; json += "\",";
  json += "\"heaterReady\":" + String(relayIsReady() ? "true" : "false") + ",";
  json += "\"heaterOn\":" + String(relayIsOn() ? "true" : "false") + ",";
  json += "\"homed\":" + String(homingIsDone() ? "true" : "false") + ",";
  json += "\"opSpeedUnits\":" + String(gSettings.routineSpeedUnits) + ",";
  json += "\"preheatMult\":" + String(gSettings.preheatMult, 2) + ",";
  json += "\"returnSpeedPct\":" + String(gSettings.returnSpeedPct) + ",";
  json += "\"edgeKeepoffCounts\":" + String(gSettings.edgeKeepoffCounts) + ",";
  json += "\"parkOffsetCounts\":" + String(gSettings.parkOffsetCounts) + ",";
  json += "\"heaterDelayMs\":" + String(gSettings.heaterDelayMs) + ",";
  json += "\"passIndex\":" + String(gWaxPassIndex) + ",";
  json += "\"passesReq\":" + String(gWaxPassesReq);
  json += "}";
  return json;
}