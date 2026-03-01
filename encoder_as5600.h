#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "pins.h"

#define AS5600_ADDRESS 0x36

static volatile long g_encoderCounts = 0;
static uint32_t g_lastEncMs = 0;
static const uint32_t ENC_PERIOD_MS = 2;

static inline void encoderBegin() {
  Wire.begin(I2C_SDA, I2C_SCL);
}

static inline void encoderReadNow() {
  int raw = 0;
  static int prev = 0;
  static long revs = 0;

  Wire.beginTransmission(AS5600_ADDRESS);
  Wire.write(0x0C);
  Wire.endTransmission(false);

  Wire.requestFrom(AS5600_ADDRESS, 2);
  if (Wire.available() < 2) return;

  raw = (Wire.read() << 8) | Wire.read();
  raw &= 0x0FFF;

  if (prev > 3000 && raw < 1000) revs++;
  else if (prev < 1000 && raw > 3000) revs--;

  prev = raw;
  g_encoderCounts = (long)raw + (4096L * revs);
}

static inline void encoderTask() {
  uint32_t now = millis();
  if (now - g_lastEncMs >= ENC_PERIOD_MS) {
    g_lastEncMs = now;
    encoderReadNow();
  }
}

static inline long encoderGetCounts() {
  return (long)g_encoderCounts;
}