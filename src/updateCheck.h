#ifndef UPDATE_CHECK_H
#define UPDATE_CHECK_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

typedef bool (*SessionCheckFn)(AsyncWebServerRequest *);

void setupUpdateEndpoint(AsyncWebServer &server, SessionCheckFn sessionCheck);
void startOTA();

#endif
