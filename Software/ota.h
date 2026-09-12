#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>

static bool g_otaRejected = false;

static inline bool otaAllowedNow(){
  return true;
}

static inline void handleFwUpdateRequest(AsyncWebServerRequest *request) {
  bool ok = (!Update.hasError()) && !g_otaRejected;

  AsyncWebServerResponse *response =
    request->beginResponse(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
  response->addHeader("Connection", "close");
  request->send(response);

  if (ok) {
    delay(300);
    ESP.restart();
  }
}

static inline void handleFwUpload(AsyncWebServerRequest *request,
                                  String filename, size_t index,
                                  uint8_t *data, size_t len, bool final)
{
  if (index == 0) {
    g_otaRejected = false;

    if (!otaAllowedNow()) { g_otaRejected = true; return; }
    if (!filename.endsWith(".bin")) { g_otaRejected = true; return; }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      g_otaRejected = true;
      return;
    }
  }

  if (!g_otaRejected && len) {
    size_t written = Update.write(data, len);
    if (written != len) {
      g_otaRejected = true;
      return;
    }
  }

  if (final) {
    if (g_otaRejected) return;
    if (!Update.end(true)) {
      g_otaRejected = true;
    }
  }
}