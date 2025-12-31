#include "Utils.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>



// Simple helper to reboot device
void rebootDevice()
{
    Serial.println("[SYSTEM] Rebooting...");
    delay(500); // Let prints/HTTP finish
    ESP.restart();
}

// Simple helper to check internet connectivity
bool isInternetAvailable(unsigned long timeout)
{
    if (WiFi.status() != WL_CONNECTED)
        return false;

    HTTPClient http;
    http.setConnectTimeout(timeout);

    if (http.begin("http://clients3.google.com/generate_204"))
    {
        int httpCode = http.GET();
        http.end();
        if (httpCode == 204)
        {
            return true; // Internet is OK
        }
    }
    return false; // No internet
}

void initNTP(const char *ntpServer, long gmtOffset_sec, int daylightOffset_sec)
{
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("[NTP] Time sync started...");
}

bool isTimeSynced()
{
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    return (timeinfo.tm_year + 1900) > 2023; // year check
}

String getTimeString()
{
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    return String(buf);
}