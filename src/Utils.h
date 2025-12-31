#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <esp_system.h>  // For ESP.restart()


// Reboot the ESP32 safely
void rebootDevice();
// Check if internet is available by making a simple HTTP request
bool isInternetAvailable(unsigned long timeout = 2000);

#pragma once
void initNTP(const char* ntpServer = "pool.ntp.org", long gmtOffset_sec = 19800, int daylightOffset_sec = 0);
bool isTimeSynced();
String getTimeString();

#endif 
