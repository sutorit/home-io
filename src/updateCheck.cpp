#include "updateCheck.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

#define OTA_URL "https://github.com/YOUR_USERNAME/YOUR_REPO/releases/latest/download/firmware.bin"

void setupUpdateEndpoint(AsyncWebServer &server)
{
    server.on("/checkUpdate", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        // 🔐 reuse your existing session system
        if (!hasValidSession(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }

        if (WiFi.status() != WL_CONNECTED) {
            request->send(500, "text/plain", "WiFi not connected");
            return;
        }

        request->send(200, "text/plain", "Firmware update started...");
        delay(200);
        startOTA();
    });
}

void startOTA()
{
    Serial.println("[OTA] Starting update...");

    HTTPClient http;
    http.begin(OTA_URL);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] HTTP error: %d\n", httpCode);
        http.end();
        return;
    }

    int contentLength = http.getSize();
    WiFiClient *client = http.getStreamPtr();

    if (!Update.begin(contentLength)) {
        Serial.println("[OTA] Not enough space");
        http.end();
        return;
    }

    size_t written = Update.writeStream(*client);

    if (written != contentLength) {
        Serial.println("[OTA] Write failed");
        Update.abort();
        http.end();
        return;
    }

    if (Update.end() && Update.isFinished()) {
        Serial.println("[OTA] Update successful. Rebooting...");
        delay(500);
        ESP.restart();
    }

    http.end();
}
