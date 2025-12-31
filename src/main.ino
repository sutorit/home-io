#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "configManager.h"
#include "webServer.h"
#include "switchControl.h"
#include "wifiManager.h"
#include "mqttHandler.h"
#include "firebaseHandler.h"
#include "systemState.h"
#include "schedule.h"
#include "Utils.h"
#include "usagesHandler.h"

UsageHandler usageHandler(config.acs712Pin, config.acs712Sensitivity, config.zeroCrossPin);
bool ntpReady = false;

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.print("\n[SUTORIT ");
  Serial.print(MAX_CHANNELS);
  Serial.println("] Starting up...");

  if (!configManager.load())
  {
    config.OnlineStatus = "Offline";
    Serial.println("[CONFIG] No config found. Starting AP mode...");
    String apName = "SUTORIT_" + String((uint32_t)ESP.getEfuseMac(), HEX).substring(4);
    WiFi.softAP(apName.c_str(), "12345678");

    Serial.print("[AP] SSID: ");
    Serial.println(apName);
    Serial.print("[AP] IP: ");
    Serial.println(WiFi.softAPIP()); // <-- Show AP IP

    webServer.startAPMode();
    Serial.println("[CONFIG] Loaded config.");
    switchControl.begin();
    systemState.no_config = true;
    return; // Stay in AP mode until config done
  }

  // if (config.mode == "master")
  // {
  //   Serial.println("[MODE] Master");
  //   wifiManager.startMaster();
  // }
  // else if (config.mode == "slave")
  // {
  //   Serial.println("[MODE] Slave");
  //   meshManager.startSlave("SUTORIT_MASTER", "12345678");
  // }
  wifiManager.startWiFi();
  webServer.startNormalMode();
  systemState.no_config = false;
  // Give WiFi time to connect before printing IP
  delay(500);
  Serial.print("[WiFi] Connected. IP: ");
  Serial.println(WiFi.localIP()); // <-- Show STA IP

  Serial.println("[CONFIG] Loaded config.");
  switchControl.begin();
  // esp_task_wdt_init(15, true); // 15s watchdog
  // esp_task_wdt_add(NULL);      // add current loopTask

  initNTP();
  // usageHandler.begin();
}

void loop()
{
  // 🔹 Core loops
  switchControl.loop();

  if (!systemState.no_config)
  {
    wifiManager.loop();
  }

  // 🔹 Schedule MUST run continuously (after NTP sync)
  if (!systemState.no_config && ntpReady)
  {
    schedulemanager.loop();
  }

  // static bool recoveryDone = false;

  // if (!recoveryDone && ntpReady && systemState.firebaseReady)
  // {
  //   schedulemanager.recoverMissed();
  //   recoveryDone = true;
  // }

  static unsigned long lastCheck = 0;
  static bool internetOk = false;

  // 🔹 Internet check every 10 seconds
  if (millis() - lastCheck > 10000)
  {
    lastCheck = millis();
    internetOk = isInternetAvailable(3000);

    if (!internetOk && systemState.servicesStarted)
    {
      Serial.println("[NET] Internet lost! Stopping services...");
      mqttHandler.stop();
      firebaseHandler.stop();
      systemState.mqttReady = false;
      systemState.firebaseReady = false;
      systemState.servicesStarted = false;
      config.OnlineStatus = "Offline";
    }
    else if (internetOk && !systemState.servicesStarted)
    {
      Serial.println("[NET] Internet restored! Will re-init services...");
      config.OnlineStatus = "Online";
    }
  }

  // 🔹 Start MQTT + Firebase ONLY ONCE when internet is OK
  if (!systemState.servicesStarted && WiFi.status() == WL_CONNECTED && internetOk)
  {
    // ---- MQTT ----
    if (config.mqtt_broker.length() > 0 &&
        config.mqtt_user.length() > 0 &&
        config.mqtt_pass.length() > 0 &&
        config.mqtt_port > 0)
    {
      Serial.println("[CONFIG] MQTT initialized");
      mqttHandler.begin();
      systemState.mqttReady = true;
    }

    // ---- Firebase ----
    if (config.firebase_url.length() > 0 &&
        config.firebase_key.length() > 0)
    {
      Serial.println("[CONFIG] Firebase initialized");
      firebaseHandler.begin();
      systemState.firebaseReady = true;
    }

    systemState.servicesStarted = true;
  }

  // 🔹 Keep services running
  if (systemState.servicesStarted)
  {
    if (systemState.mqttReady)
    {
      mqttHandler.loop();
      if (!mqttHandler.isReady())
      {
        Serial.println("[MQTT] Lost connection, restarting...");
        mqttHandler.stop();
        systemState.mqttReady = false;
        systemState.servicesStarted = false;
      }
    }

    if (systemState.firebaseReady)
    {
      firebaseHandler.loop();
      if (!firebaseHandler.isReady())
      {
        Serial.println("[Firebase] Not ready, restarting...");
        firebaseHandler.stop();
        systemState.firebaseReady = false;
        systemState.servicesStarted = false;
      }
    }
  }

  // 🔹 NTP sync checker (every 5s until synced)
  static unsigned long lastTimeCheck = 0;
  if (!ntpReady && millis() - lastTimeCheck > 5000)
  {
    lastTimeCheck = millis();
    if (isTimeSynced())
    {
      ntpReady = true;
      Serial.println("[NTP] Time synced! Schedule ENABLED");
    }
  }

  // 🔹 Heap monitor every 60 seconds
  static unsigned long lastHeapLog = 0;
  if (millis() - lastHeapLog > 60000)
  {
    lastHeapLog = millis();

    size_t freeHeap = ESP.getFreeHeap();
    size_t minFreeHeap = ESP.getMinFreeHeap();
    size_t maxAlloc = ESP.getMaxAllocHeap();

    Serial.printf("[Heap] Free: %d, Min Free: %d, Max Alloc: %d\n",
                  freeHeap, minFreeHeap, maxAlloc);

    if (freeHeap < 20000 || maxAlloc < 5000)
    {
      Serial.println("[MEM] Low memory detected! Running cleanup...");

      yield();
      delay(10);
      WiFi.disconnect(false);
      WiFi.reconnect();

      if (systemState.mqttReady)
      {
        mqttHandler.stop();
        systemState.mqttReady = false;
      }

      if (systemState.firebaseReady)
      {
        firebaseHandler.stop();
        systemState.firebaseReady = false;
      }

      systemState.servicesStarted = false;
      Serial.println("[MEM] Cleanup done, services will restart automatically.");
    }
  }
}

bool isValidHostnameOrIP(const String &host)
{
  if (host.length() == 0)
    return false;

  // 1. Check if it's a valid IP
  IPAddress ip;
  if (ip.fromString(host))
  {
    return true; // valid IP
  }

  // 2. Check if it's a valid hostname (letters, digits, dots, hyphens)
  for (size_t i = 0; i < host.length(); i++)
  {
    char c = host.charAt(i);
    if (!(isalnum(c) || c == '.' || c == '-'))
    {
      return false;
    }
  }

  // Hostnames can’t start/end with dot or hyphen
  if (host.startsWith(".") || host.startsWith("-") ||
      host.endsWith(".") || host.endsWith("-"))
  {
    return false;
  }

  return true;
}
