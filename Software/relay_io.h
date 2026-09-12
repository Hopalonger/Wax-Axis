#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_I2C_Expander_Arduino_Library.h>

// Qwiic GPIO expander (PCA9534-class) drives IR heater relay.
static const uint8_t RELAY_I2C_ADDR     = 0x27;
static const uint8_t RELAY_IO_PIN       = 4;
static const bool    RELAY_ACTIVE_HIGH  = true;

static SFE_PCA95XX gRelayIo(PCA95XX_PCA9534);
static bool gRelayReady = false;
static bool gRelayOn    = false;

static inline bool relayBegin() {
  if (!gRelayIo.begin(RELAY_I2C_ADDR, Wire)) {
    gRelayReady = false;
    gRelayOn = false;
    return false;
  }
  gRelayIo.pinMode(RELAY_IO_PIN, OUTPUT);
  gRelayReady = true;
  gRelayOn = false;
  // default OFF
  bool driveHigh = RELAY_ACTIVE_HIGH ? false : true;
  gRelayIo.digitalWrite(RELAY_IO_PIN, driveHigh ? HIGH : LOW);
  return true;
}

static inline void relaySet(bool on) {
  if (!gRelayReady) return;
  bool driveHigh = RELAY_ACTIVE_HIGH ? on : !on;
  gRelayIo.digitalWrite(RELAY_IO_PIN, driveHigh ? HIGH : LOW);
  gRelayOn = on;
}

static inline bool relayIsReady() { return gRelayReady; }
static inline bool relayIsOn()    { return gRelayOn; }