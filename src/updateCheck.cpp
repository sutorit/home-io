#include "updateCheck.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "configManager.h"

#define OTA_URL "https://github.com/sutorit/home-io/releases/latest/download/firmware.bin"

static SessionCheckFn _sessionCheck;
static String g_latestNotes = "";

volatile uint8_t otaProgress = 0;
volatile bool otaRunning = false;
static String g_latestVersion = "";

/* ================= RELEASE INFO ================= */

struct ReleaseInfo
{
    String version;
    String notes; // raw notes (multiline)
};

/* ---------- helpers ---------- */

String normalizeVersion(String v)
{
    v.trim();
    v.toUpperCase();
    if (v.startsWith("V"))
        v = v.substring(1);
    return v;
}

/* ---------- fetch release ---------- */

ReleaseInfo fetchLatestRelease()
{
    ReleaseInfo info;
    HTTPClient http;

    http.begin("https://api.github.com/repos/sutorit/home-io/releases/latest");
    http.addHeader("User-Agent", "ESP32-OTA");

    int code = http.GET();
    if (code != 200)
    {
        http.end();
        return info;
    }

    String payload = http.getString();
    http.end();

    // ---- tag_name ----
    int vIdx = payload.indexOf("\"tag_name\":\"");
    if (vIdx >= 0)
    {
        vIdx += 12;
        int vEnd = payload.indexOf("\"", vIdx);
        info.version = payload.substring(vIdx, vEnd);
    }

    // ---- body ----
    int bIdx = payload.indexOf("\"body\":\"");
    if (bIdx >= 0)
    {
        bIdx += 8;
        int bEnd = payload.indexOf("\",\"", bIdx);
        if (bEnd < 0)
            bEnd = payload.indexOf("\"}", bIdx); // fallback

        if (bEnd > bIdx)
        {
            info.notes = payload.substring(bIdx, bEnd);
            info.notes.replace("\\r", "");
            info.notes.replace("\\n", "\n");
            info.notes.replace("\\\"", "\"");
        }
    }

    if (info.notes.length() > 500)
        info.notes = info.notes.substring(0, 500);

    return info;
}

/* ================= UPDATE CHECK ================= */

bool updateAvailable(const String &latest)
{
    if (latest.length() == 0)
        return false;

    return normalizeVersion(latest) !=
           normalizeVersion(config.firmware_version);
}

/* ================= HTTP ENDPOINTS ================= */

void setupUpdateEndpoint(AsyncWebServer &server, SessionCheckFn sessionCheck)
{
    _sessionCheck = sessionCheck;

    /* -------- CHECK UPDATE -------- */
    server.on("/checkUpdate", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (!_sessionCheck(request))
        {
            request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
            return;
        }

        if (WiFi.status() != WL_CONNECTED)
        {
            request->send(500, "application/json", "{\"error\":\"wifi\"}");
            return;
        }

        ReleaseInfo release = fetchLatestRelease();
        g_latestVersion = release.version;
        g_latestNotes = release.notes;

        bool hasUpdate = updateAvailable(release.version);

        // ---- build JSON ----
        String json = "{";
        json += "\"update\":" + String(hasUpdate ? "true" : "false") + ",";
        json += "\"current\":\"" + config.firmware_version + "\",";
        json += "\"latest\":\"" + release.version + "\",";
        json += "\"notes\":[";

        if (release.notes.length())
        {
            int start = 0;
            while (true)
            {
                int end = release.notes.indexOf('\n', start);
                String line = (end == -1)
                              ? release.notes.substring(start)
                              : release.notes.substring(start, end);

                line.trim();
                if (line.length())
                    json += "\"" + line + "\",";

                if (end == -1)
                    break;
                start = end + 1;
            }

            if (json.endsWith(","))
                json.remove(json.length() - 1);
        }

        json += "]}";

        request->send(200, "application/json", json); });

    /* -------- START OTA -------- */
    server.on("/startUpdate", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (!_sessionCheck(request))
        {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }

        request->send(200, "text/plain", "OTA started");

        xTaskCreate(
            [](void *)
            {
                startOTA();
                vTaskDelete(NULL);
            },
            "OTA_TASK",
            20480,
            NULL,
            1,
            NULL
        ); });

    /* -------- OTA PROGRESS -------- */
    server.on("/otaProgress", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (!_sessionCheck(request))
        {
            request->send(401, "application/json", "{\"error\":1}");
            return;
        }

        String json = "{";
        json += "\"running\":" + String(otaRunning ? "true" : "false") + ",";
        json += "\"progress\":" + String(otaProgress);
        json += "}";

        request->send(200, "application/json", json); });
}

/* ================= OTA CORE ================= */

void startOTA()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    otaRunning = true;
    otaProgress = 0;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    if (!http.begin(client, OTA_URL))
    {
        otaRunning = false;
        return;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        http.end();
        otaRunning = false;
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0)
    {
        http.end();
        otaRunning = false;
        return;
    }

    if (!Update.begin(contentLength))
        if (!Update.begin(contentLength))
        {
            http.end();
            otaRunning = false;
            return;
        }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[1024];
    size_t written = 0;

    Update.onProgress([](size_t done, size_t total)
                      {
        if (total)
            otaProgress = (done * 100) / total; });

    while (http.connected() && written < contentLength)
    {
        size_t available = stream->available();
        if (available)
        {
            int len = stream->readBytes(buffer, min(available, sizeof(buffer)));
            written += Update.write(buffer, len);
        }
        delay(1);
        yield();
    }

    if (!Update.end(true))
    {
        http.end();
        otaRunning = false;
        return;
    }

    // ---- SAVE VERSION ----
    config.last_firmware_version = config.firmware_version;
    config.firmware_version = g_latestVersion;

    // Save release notes
    config.last_release_notes = g_latestNotes;

    // Simple timestamp
    config.last_update_time = String(millis());

    configManager.save();

    otaProgress = 100;
    delay(800);
    ESP.restart();
}
