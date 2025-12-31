#ifndef UPDATE_CHECK_H
#define UPDATE_CHECK_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Tell compiler this function exists elsewhere
bool hasValidSession(AsyncWebServerRequest *req);

void setupUpdateEndpoint(AsyncWebServer &server);
void startOTA();

#endif
