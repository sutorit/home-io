#include "updateCheck.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

#define OTA_URL "https://github.com/sutorit/sutorit-home/releases/latest/download/firmware.bin"

static SessionCheckFn _sessionCheck;

void setupUpdateEndpoint(AsyncWebServer &server, SessionCheckFn sessionCheck)
{
    _sessionCheck = sessionCheck;

    server.on("/checkUpdate", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        // 🔐 Auth check
        if (!_sessionCheck(request))
        {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            request->send(500, "text/plain", "WiFi not connected");
            return;
        }

        request->send(200, "text/plain", "OTA started");
        delay(300);

        startOTA();
    });
}

void startOTA()
{
    Serial.println("[OTA] Checking for update...");

    HTTPClient http;
    http.begin(OTA_URL);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("[OTA] HTTP error: %d\n", httpCode);
        http.end();
        return;
    }

    int contentLength = http.getSize();
    WiFiClient *client = http.getStreamPtr();

    if (!Update.begin(contentLength))
    {
        Serial.println("[OTA] Not enough space");
        http.end();
        return;
    }

    size_t written = Update.writeStream(*client);
    if (written != contentLength)
    {
        Serial.println("[OTA] Write failed");
        Update.abort();
        http.end();
        return;
    }

    if (!Update.end())
    {
        Serial.println("[OTA] Update failed");
        http.end();
        return;
    }

    if (Update.isFinished())
    {
        Serial.println("[OTA] Success! Rebooting...");
        delay(500);
        ESP.restart();
    }

    http.end();
}
