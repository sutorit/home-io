#include "updateCheck.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "configManager.h"

#define OTA_URL "https://github.com/sutorit/sutorit-home/releases/latest/download/firmware.bin"

static SessionCheckFn _sessionCheck;

volatile uint8_t otaProgress = 0;
volatile bool otaRunning = false;

void setupUpdateEndpoint(AsyncWebServer &server, SessionCheckFn sessionCheck)
{
    _sessionCheck = sessionCheck;

    // Check update
    server.on("/checkUpdate", HTTP_GET, [](AsyncWebServerRequest *request)
              {
    if (!_sessionCheck(request)) {
        request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        request->send(500, "application/json", "{\"error\":\"wifi\"}");
        return;
    }

   String json = "{";
        json += "\"update\":true,";
        json += "\"current\":\"" + String(FIRMWARE_VERSION) + "\",";
        json += "\"notes\":[";
        json += "\"Improved stability\",";
        json += "\"OTA progress UI added\",";
        json += "\"Performance optimized\"";
        json += "]";
        json += "}";


    request->send(200, "application/json", json); });

    // Start OTA
    server.on("/startUpdate", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (!_sessionCheck(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }

        request->send(200, "text/plain", "OTA started");

        xTaskCreate(
            [](void*) {
                startOTA();
                vTaskDelete(NULL);
            },
            "OTA_TASK",
            20480,
            NULL,
            1,
            NULL
        ); });

    // OTA progress
    server.on("/otaProgress", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (!_sessionCheck(request)) {
            request->send(401, "application/json", "{\"error\":1}");
            return;
        }

        String json = "{";
        json += "\"running\":" + String(otaRunning ? "true" : "false") + ",";
        json += "\"progress\":" + String(otaProgress);
        json += "}";

        request->send(200, "application/json", json); });
}

void startOTA()
{
    if (WiFi.status() != WL_CONNECTED) return;

    otaRunning = true;
    otaProgress = 0;

    Serial.println("[OTA] Starting OTA");
    Serial.printf("[OTA] Free heap: %u\n", ESP.getFreeHeap());
    Serial.printf("[OTA] Free sketch space: %u\n", ESP.getFreeSketchSpace());

    WiFiClientSecure client;
    client.setInsecure(); // GitHub HTTPS

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    if (!http.begin(client, OTA_URL)) {
        Serial.println("[OTA] http.begin failed");
        otaRunning = false;
        return;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] HTTP error: %d\n", httpCode);
        http.end();
        otaRunning = false;
        return;
    }

    int contentLength = http.getSize();
    Serial.printf("[OTA] Content length: %d\n", contentLength);

    if (!Update.begin(contentLength, U_FLASH)) {
        Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
        http.end();
        otaRunning = false;
        return;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[1024];
    size_t writtenTotal = 0;

    Update.onProgress([](size_t done, size_t total) {
        if (total > 0) otaProgress = (done * 100) / total;
    });

    while (http.connected() && writtenTotal < contentLength) {
        size_t available = stream->available();
        if (available) {
            int len = stream->readBytes(buffer, min(available, sizeof(buffer)));
            size_t written = Update.write(buffer, len);
            writtenTotal += written;
        }
        delay(1);   // REQUIRED
        yield();    // REQUIRED
    }

    if (!Update.end(true)) {
        Serial.printf("[OTA] Update failed: %s\n", Update.errorString());
        http.end();
        otaRunning = false;
        return;
    }

    Serial.println("[OTA] Update successful");
    otaProgress = 100;
    delay(800);
    ESP.restart();
}

