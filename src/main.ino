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
#include <Arduino.h>
#include <Wire.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"

UsageHandler usageHandler(config.acs712Pin, config.acs712Sensitivity, config.zeroCrossPin);
bool ntpReady = false;

static unsigned long lastServiceFail = 0;
static const unsigned long SERVICE_RESTART_DELAY = 30000; // 30 sec

// Main setup function runs once at startup
void setup()
{
  Serial.begin(115200);
  delay(1000);
  const esp_partition_t *p = esp_ota_get_running_partition();              // Get current OTA partition
  Serial.printf("Running partition: %s @ 0x%08X\n", p->label, p->address); // Show partition info for debugging
  Serial.print("\n[SUTORIT ");                                             // <-- fun branding :)
  Serial.print(String(MAX_CHANNELS) + "CH");                               // <-- Show number of channels in log for easier debugging
  Serial.println("] Starting up...");                                      // <-- Show startup message with channel count

  // Check if config exists
  if (!configManager.load())
  {
    config.OnlineStatus = "Offline"; // Set offline status if no config (AP mode)
    Serial.println("[CONFIG] No config found. Starting AP mode...");
    webServer.startAPMode(); // Stay in AP mode until config done
    Serial.println("[CONFIG] Loaded config.");
    switchControl.begin();        // Start switch control even in AP mode (for config purposes)
    systemState.no_config = true; // Mark that we are in AP mode with no config
    return;                       // Stay in AP mode until config done
  }

  // Normal startup with config
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
  Wire.begin(config.i2c_sda, config.i2c_scl); // For I2C comunications
}

// Main loop runs continuously after setup
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
  if (!systemState.servicesStarted &&
      WiFi.status() == WL_CONNECTED &&
      internetOk &&
      millis() - lastServiceFail > SERVICE_RESTART_DELAY)

  {
    // ---- MQTT ----
    if (config.mqtt_enable)
    {
      if (config.mqtt_broker.length() > 0 &&
          config.mqtt_user.length() > 0 &&
          config.mqtt_pass.length() > 0 &&
          config.mqtt_port > 0)
      {
        Serial.println("[CONFIG] MQTT initialized");
        mqttHandler.begin();
        systemState.mqttReady = true;
      }
      else
      {
        config.mqtt_enable = false; // Disable MQTT if config is incomplete
        Serial.println("[CONFIG] MQTT config incomplete, cannot start MQTT");
      }
    }

    // ---- Firebase ----
    if (config.firebase_enable)
    {
      if (config.firebase_url.length() > 0 &&
          config.firebase_key.length() > 0)
      {
        Serial.println("[CONFIG] Firebase initialized");
        firebaseHandler.begin();
        systemState.firebaseReady = true;
      }else
      {
        config.firebase_enable = false; // Disable Firebase if config is incomplete
        Serial.println("[CONFIG] Firebase config incomplete, cannot start Firebase");
      }
    }

    systemState.servicesStarted = true;
  }

  // 🔹 Keep services running (SAFE)
  if (systemState.servicesStarted)
  {
    bool serviceFailed = false;

    // ---- MQTT ----
    if (systemState.mqttReady)
    {
      mqttHandler.loop();

      if (!mqttHandler.isReady())
      {
        Serial.println("[MQTT] Lost connection");
        mqttHandler.stop();
        systemState.mqttReady = false;
        serviceFailed = true;
      }
    }

    // ---- Firebase ----
    if (systemState.firebaseReady)
    {
      firebaseHandler.loop();

      if (!firebaseHandler.isReady())
      {
        Serial.println("[Firebase] Lost connection");
        firebaseHandler.stop();
        systemState.firebaseReady = false;
        serviceFailed = true;
      }
    }

    // ---- Handle failure ONCE ----
    if (serviceFailed)
    {
      systemState.servicesStarted = false;
      lastServiceFail = millis();
      Serial.println("[SERVICES] Marked stopped, waiting before restart");
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
      // WiFi.disconnect(false);
      // WiFi.reconnect();

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
