#include <Arduino.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "Utils.h"
#include "webServer.h"
#include "switchControl.h"
#include "configManager.h"
#include "systemState.h"
#include "usagesHandler.h"
#include "updateCheck.h"

extern UsageHandler usageHandler;

AsyncWebServer server(80);
WebServer webServer;

const byte DNS_PORT = 53;
DNSServer dnsServer;

// ---------------- Session handling ----------------
String activeSessionToken = "";

// Simple random token generator
String generateSessionToken()
{
    String token = "";
    for (int i = 0; i < 32; i++)
    {
        token += char('A' + random(26));
    }
    return token;
}

// Check if request has valid session cookie
bool hasValidSession(AsyncWebServerRequest *req)
{
    if (!req->hasHeader("Cookie"))
        return false;
    AsyncWebHeader *cookie = req->getHeader("Cookie");
    if (cookie->value().indexOf("SESSIONID=" + activeSessionToken) != -1)
    {
        return true;
    }
    return false;
}

struct RelayCommand
{
    int ch;
    bool state; // true = on, false = off
};

QueueHandle_t relayQueue; // to store incoming commands

// Common routes (used in both AP and STA modes)
void WebServer::registerRoutes()
{
    // Helper for conditional auth
    auto serveWithAuth = [](AsyncWebServerRequest *req, const char *file)
    {
        if (systemState.no_config)
        {
            req->send(SPIFFS, file, "text/html");
            return;
        }

        // Check session cookie
        if (!hasValidSession(req))
        {
            req->redirect("/login.html");
            return;
        }

        // Auth passed → serve page
        req->send(SPIFFS, file, "text/html");
    };

    // ---------------- Routes ----------------
    server.serveStatic("/style.css", SPIFFS, "/style.css")
        .setCacheControl("max-age=86400");
    server.serveStatic("/theme.js", SPIFFS, "/theme.js")
        .setCacheControl("max-age=86400");
    server.serveStatic("/sutorit.png", SPIFFS, "/sutorit.png")
        .setCacheControl("max-age=86400");
    server.serveStatic("/login.html", SPIFFS, "/login.html")
        .setCacheControl("no-cache"); // login should never be cached

    // Secure pages
    server.on("/", HTTP_GET, [serveWithAuth](AsyncWebServerRequest *req)
              { serveWithAuth(req, "/index.html"); });
    server.on("/settings.html", HTTP_GET, [serveWithAuth](AsyncWebServerRequest *req)
              { serveWithAuth(req, "/settings.html"); });
    server.on("/config.html", HTTP_GET, [serveWithAuth](AsyncWebServerRequest *req)
              { serveWithAuth(req, "/config.html"); });
    server.on("/info.html", HTTP_GET, [serveWithAuth](AsyncWebServerRequest *req)
              { serveWithAuth(req, "/info.html"); });
    server.on("/reboot.html", HTTP_GET, [serveWithAuth](AsyncWebServerRequest *req)
              { serveWithAuth(req, "/reboot.html"); });
    server.on("/settingsutils.html", HTTP_GET, [serveWithAuth](AsyncWebServerRequest *req)
              { serveWithAuth(req, "/settingsutils.html"); });

    // Login handler
    server.on("/login", HTTP_POST, [](AsyncWebServerRequest *req)
              {
    if (req->hasParam("username", true) && req->hasParam("password", true)) {
        String user = req->getParam("username", true)->value();
        String pass = req->getParam("password", true)->value();

        if (user == config.device_name && pass == config.device_password) {
            activeSessionToken = generateSessionToken();
            AsyncWebServerResponse *res = req->beginResponse(302);  // 302 = redirect
            res->addHeader("Location", "/");  // redirect to home page
            res->addHeader("Set-Cookie", "SESSIONID=" + activeSessionToken + "; Path=/; Max-Age=86400");
            req->send(res);
            return;
        }
    }
    req->send(401, "text/plain", "Invalid credentials"); });

    // Logout handler
    server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *req)
              {
    activeSessionToken = ""; // invalidate session
    // Create a redirect response
    AsyncWebServerResponse *res = req->beginResponse(302); // 302 = Found (redirect)
    res->addHeader("Location", "/login.html");
    // Clear cookie
    res->addHeader("Set-Cookie", "SESSIONID=deleted; Path=/; Max-Age=0");
    req->send(res); });

    // Save config
    server.on("/saveConfig", HTTP_POST, [](AsyncWebServerRequest *req)
              {
    if (req->hasParam("device_name", true)) config.device_name = req->getParam("device_name", true)->value();
    if (req->hasParam("device_password", true)) config.device_password = req->getParam("device_password", true)->value();
    if (req->hasParam("mode", true)) config.mode = req->getParam("mode", true)->value();
    if (req->hasParam("master_ip", true)) config.master_ip = req->getParam("master_ip", true)->value();

    if (req->hasParam("wifi_ssid", true)) config.wifi_ssid = req->getParam("wifi_ssid", true)->value();
    if (req->hasParam("wifi_password", true)) config.wifi_password = req->getParam("wifi_password", true)->value();
    if (req->hasParam("wifi_ip", true)) config.device_ip = req->getParam("wifi_ip", true)->value();
    if (req->hasParam("wifi_ipgetway", true)) config.gateway_ip = req->getParam("wifi_ipgetway", true)->value();
    if (req->hasParam("wifi_subnetmask", true)) config.subnetMask = req->getParam("wifi_subnetmask", true)->value();
    if (req->hasParam("wifi_dns", true)) config.dns = req->getParam("wifi_dns", true)->value();

    if (req->hasParam("mqtt_broker", true)) config.mqtt_broker = req->getParam("mqtt_broker", true)->value();
    if (req->hasParam("mqtt_port", true)) config.mqtt_port = req->getParam("mqtt_port", true)->value().toInt();
    if (req->hasParam("mqtt_user", true)) config.mqtt_user = req->getParam("mqtt_user", true)->value();
    if (req->hasParam("mqtt_pass", true)) config.mqtt_pass = req->getParam("mqtt_pass", true)->value();

    if (req->hasParam("firebase_url", true)) config.firebase_url = req->getParam("firebase_url", true)->value();
    if (req->hasParam("firebase_key", true)) config.firebase_key = req->getParam("firebase_key", true)->value();

    // --- Save per-switch configuration ---
    for (int i = 0; i < MAX_CHANNELS; i++) {
            // Output mapping (OUTPUT 1, OUTPUT 2, etc.)
            String outputParam = "switch_output_" + String(i + 1);
            if (req->hasParam(outputParam, true)) {
                config.switch_to_output[i] = req->getParam(outputParam, true)->value().toInt();
            }

            // AC/DC type (ac or dc)
            String typeParam = "switch_type_" + String(i + 1);
            if (req->hasParam(typeParam, true)) {
                config.switch_type[i] = req->getParam(typeParam, true)->value();
            }

            // Switch logic (toggle, bell, 2way, etc.)
            String logicParam = "switch_logic_" + String(i + 1);
            if (req->hasParam(logicParam, true)) {
                config.switch_logic[i] = req->getParam(logicParam, true)->value();
            }
        }


    // Save to SPIFFS
    if (configManager.save())
        Serial.println("[CONFIG] Configuration saved successfully.");
    else
        Serial.println("[CONFIG] Error saving configuration!");

    req->redirect("/");
    rebootDevice(); });

    // Get current config (for config page)
    server.on("/getConfig", HTTP_GET, [](AsyncWebServerRequest *req)
              {
    DynamicJsonDocument doc(2048);

    // Basic device info
    doc["relayCount"]       = MAX_CHANNELS;
    doc["deviceName"]       = config.device_name;
    doc["devicePassword"]   = config.device_password;
    doc["deviceId"]         = config.device_id;
    doc["ip"]               = config.device_ip;
    doc["gatewayIP"]        = config.gateway_ip;
    doc["subnetMask"]       = config.subnetMask;
    doc["dns"]              = config.dns;
    doc["mode"]             = config.mode;
    doc["masterIP"]         = config.master_ip;
    doc["ssid"]             = config.wifi_ssid;
    doc["password"]         = config.wifi_password;

    // MQTT config
    doc["mqttBroker"]       = config.mqtt_broker;
    doc["mqttPort"]         = config.mqtt_port;
    doc["mqttUser"]         = config.mqtt_user;
    doc["mqttPass"]         = config.mqtt_pass;

    // Firebase config
    doc["firebaseURL"]      = config.firebase_url;
    doc["firebaseKey"]      = config.firebase_key;

    // Status
    doc["onlineStatus"]    = config.OnlineStatus;

    // Firmware version
    doc["firmwareVersion"]  = "V0.10.1.5"; // Update this when releasing new firmware!

    // Uptime calculation
    unsigned long ms = millis();
    unsigned long seconds = ms / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;

    char uptimeStr[50];
    sprintf(uptimeStr, "%lu days %02lu:%02lu:%02lu", days, hours % 24, minutes % 60, seconds % 60);
    doc["uptime"] = uptimeStr;

    // Switch map
    // Switch map with full info (output mapping + AC/DC + logic type)
    JsonArray switchArray = doc.createNestedArray("switchMap");
    for (int i = 0; i < MAX_CHANNELS; i++) {
        JsonObject s = switchArray.createNestedObject();
        s["output"] = config.switch_to_output[i] + 1;       // 1-based mapping
        s["type"]   = config.switch_type[i];                // "ac" or "dc"
        s["logic"]  = config.switch_logic[i];               // "toggle", "bell", etc.
    }


    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json); });

    // Relay control
    server.on("/relay/on", HTTP_GET, [](AsyncWebServerRequest *req)
              {
  if (req->hasParam("ch")) {
    int ch = req->getParam("ch")->value().toInt();
    RelayCommand cmd = { ch, true };
   if (relayQueue != NULL) {
    xQueueSend(relayQueue, &cmd, 0);
    req->send(200, "text/plain", "Relay queued ON");
} else {
    // fallback: perform action directly (or return 503)
    switchControl.setRelay(cmd.ch, cmd.state);
    req->send(503, "text/plain", "Queue not ready; executed directly");
}
    req->send(200, "text/plain", "Relay queued ON");
  } else {
    req->send(400, "text/plain", "Missing ch param");
  } });

    server.on("/relay/off", HTTP_GET, [](AsyncWebServerRequest *req)
              {
  if (req->hasParam("ch")) {
    int ch = req->getParam("ch")->value().toInt();
    RelayCommand cmd = { ch, false };
    if (relayQueue != NULL) {
    xQueueSend(relayQueue, &cmd, 0);
    req->send(200, "text/plain", "Relay queued ON");
} else {
    // fallback: perform action directly (or return 503)
    switchControl.setRelay(cmd.ch, cmd.state);
    req->send(503, "text/plain", "Queue not ready; executed directly");
}
    req->send(200, "text/plain", "Relay queued OFF");
  } else {
    req->send(400, "text/plain", "Missing ch param");
  } });

    server.on("/relay/toggle", HTTP_GET, [](AsyncWebServerRequest *req)
              {
        if (req->hasParam("ch")) {
            int ch = req->getParam("ch")->value().toInt();
            switchControl.toggleRelay(ch);
            req->send(200, "text/plain", "Relay " + String(ch) + " TOGGLED");
        } else req->send(400, "text/plain", "Missing ch param"); });

    // Brightness control
    server.on("/relay/brightness", HTTP_GET, [](AsyncWebServerRequest *req)
              {
    if (req->hasParam("ch") && req->hasParam("val")) {
        int ch = req->getParam("ch")->value().toInt();
        int val = req->getParam("val")->value().toInt(); // 0–100
        switchControl.setBrightness(ch, val);
        req->send(200, "text/plain", "Relay " + String(ch) + " Brightness set to " + String(val) + "%");
    } else {
        req->send(400, "text/plain", "Missing ch or val param");
    } });

    // Status
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req)
              {
    DynamicJsonDocument doc(2048);

    doc["deviceName"] = config.device_name;
    doc["relayCount"] = MAX_CHANNELS;
    doc["onlineStatus"] = config.OnlineStatus;

    JsonArray relays = doc.createNestedArray("relays");

    for (int i = 1; i <= MAX_CHANNELS; i++) {
        JsonObject relay = relays.createNestedObject();
        relay["state"] = switchControl.getState(i);       // true/false
        relay["brightness"] = switchControl.getBrightness(i); // 0-100
    }

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json); });

    // electric usages
    server.on("/eusage", HTTP_GET, [](AsyncWebServerRequest *req)
              {
    DynamicJsonDocument doc(1024);

    doc["deviceName"] = config.device_name;
    doc["current_A"] = usageHandler.getRMSCurrent();
    doc["power_W"] = usageHandler.getPower(230.0);
    doc["energy_Wh"] = usageHandler.getEnergy(230.0);

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json); });

    // Reboot
    server.on("/rebooting", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        request->send(200, "text/plain", "Rebooting...");
        rebootDevice(); });

    // Factory reset
    server.on("/factoryReset", HTTP_POST, [](AsyncWebServerRequest *req)
              {
    if (req->hasParam("username", true) && req->hasParam("password", true)) {
        String user = req->getParam("username", true)->value();
        String pass = req->getParam("password", true)->value();

        if (user == config.device_name && pass == config.device_password) {
            req->send(200, "text/plain", "Reset OK");
            configManager.factoryReset();
            return;
        } else {
            req->send(401, "text/plain", "Unauthorized");
        }
    } else {
        req->send(400, "text/plain", "Bad Request");
    } });
    // 404 for everything else
    server.onNotFound([](AsyncWebServerRequest *req)
                      { req->send(404, "text/plain", "Not found"); });
}

void relayTask(void *param)
{
    RelayCommand cmd;
    for (;;)
    {
        if (xQueueReceive(relayQueue, &cmd, portMAX_DELAY))
        {
            // Handle relay safely
            switchControl.setRelay(cmd.ch, cmd.state);
            Serial.printf("[RELAY] Channel %d set to %s\n", cmd.ch, cmd.state ? "ON" : "OFF");
            delay(10); // small delay so system stays stable
        }
    }
}

// AP mode
void WebServer::startAPMode()
{
    SPIFFS.begin(true);
    Serial.println("[AP] Server Started");
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    registerRoutes();
    server.begin();
}

// STA mode
void WebServer::startNormalMode()
{
    SPIFFS.begin(true);
    Serial.print("[WebServer] Running in STA mode. IP: ");
    Serial.println(WiFi.localIP());
    // Start mDNS
    if (!MDNS.begin(config.device_name.c_str()))
    {
        Serial.println("[MDNS] Error setting up mDNS!");
    }
    else
    {
        Serial.println("[MDNS] mDNS started: http://" + config.device_name + ".local");
    }

    relayQueue = xQueueCreate(10, sizeof(RelayCommand));      // allow 10 pending commands
    xTaskCreate(relayTask, "RelayTask", 8192, NULL, 2, NULL); // bigger stack and slightly higher prio

    registerRoutes();
    setupUpdateEndpoint(server, hasValidSession);
    server.begin();
}
