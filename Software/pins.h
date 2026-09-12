#pragma once
#include <Arduino.h>

// ===================== Access Point =====================
static const char *AP_SSID = "Wax Axis";
static const char *AP_PASS = "";

// ===================== I2C (ESP32-S3) =====================
#define I2C_SDA 8
#define I2C_SCL 9

// ===================== Pin Defines (PD Stepper board) =====================
#define TMC_EN 21
#define STEP   5
#define DIR    6
#define MS1    1
#define MS2    2
#define SPREAD 7
#define TMC_TX 17
#define TMC_RX 18
#define DIAG   16
#define INDEX  11

#define PG   15
#define CFG1 38
#define CFG2 48
#define CFG3 47

#define VBUS 4
#define LED1 10
#define LED2 12
#define SW1  35
#define SW2  36
#define SW3  37
#define AUX1 14
#define AUX2 13