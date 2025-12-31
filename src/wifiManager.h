#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

class WiFiManager {
public:
    void startWiFi();
    void startAP();
    void loop();
};

extern WiFiManager wifiManager;

#endif
