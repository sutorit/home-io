#include <WiFi.h>
#include "wifiManager.h"
#include "configManager.h"

extern "C"
{
#include "lwip_napt.h"
}

WiFiManager wifiManager;

// Track AP retry timer
unsigned long lastRetry = 0;
bool inAPMode = false;

void WiFiManager::startWiFi()
{
    if (!configManager.load())
    {
        Serial.println("[WiFi] No config found. Cannot start WiFi.");
        return;
    }
    Serial.println("[WiFi] Starting WiFi in STA mode...");
    WiFi.mode(WIFI_STA);

    // Only apply static IP if ALL parameters are provided
    if (!config.device_ip.isEmpty() &&
        !config.gateway_ip.isEmpty() &&
        !config.subnetMask.isEmpty() &&
        !config.dns.isEmpty())
    {
        IPAddress localIP, gateway, subnet, dns;
        if (localIP.fromString(config.device_ip.c_str()) &&
            gateway.fromString(config.gateway_ip.c_str()) &&
            subnet.fromString(config.subnetMask.c_str()) &&
            dns.fromString(config.dns.c_str()))
        {
            if (WiFi.config(localIP, gateway, subnet, dns))
            {
                Serial.println("[WiFi] Custom IP configuration applied.");
            }
            else
            {
                Serial.println("[WiFi] Failed to set custom IP configuration. Using DHCP...");
            }
        }
        else
        {
            Serial.println("[WiFi] Invalid IP parameters. Falling back to DHCP...");
        }
    }
    else
    {
        Serial.println("[WiFi] No custom IP set. Using DHCP...");
    }

    // Start Wi-Fi connection
    Serial.printf("[WiFi] Connecting to SSID: %s\n", config.wifi_ssid.c_str());
    WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("[WiFi] Connected! STA IP: %s\n",
                      WiFi.localIP().toString().c_str());
        config.OnlineStatus = "Online.";
        inAPMode = false;
    }
    else
    {
        Serial.println("[WiFi] Failed to connect. Switching to AP-only mode...");
        startAP();
    }
}

void WiFiManager::startAP()
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(config.device_name.c_str(), config.device_password.c_str());
    Serial.printf("[WiFi] AP-only mode active. SSID: %s, IP: %s\n",
                  config.device_name.c_str(),
                  WiFi.softAPIP().toString().c_str());
    config.OnlineStatus = "Offline (AP mode).";
    inAPMode = true;
    lastRetry = millis(); // reset retry timer
}

void WiFiManager::loop()
{
    // If STA was connected and got disconnected → fallback to AP
    if (!inAPMode && WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi] Lost STA connection. Switching to AP...");
        startAP();
    }

    // If in AP mode → retry STA every 30s
    if (inAPMode && millis() - lastRetry > 30000)
    {
        Serial.println("[WiFi] Retrying STA connection...");
        lastRetry = millis();

        WiFi.mode(WIFI_STA);
        WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 5000)
        {
            delay(500);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.printf("[WiFi] Reconnected! STA IP: %s\n",
                          WiFi.localIP().toString().c_str());
            config.OnlineStatus = "Online.";
            inAPMode = false;
        }
        else
        {
            Serial.println("[WiFi] Retry failed. Staying in AP mode.");
            WiFi.mode(WIFI_AP); // restore AP
            WiFi.softAP(config.device_name.c_str(), config.device_password.c_str());
        }
    }
}
