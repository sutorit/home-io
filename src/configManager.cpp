#include "configManager.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h> // needed for WiFi.localIP()

#define FIRMWARE_VERSION "V0.10.1.7"


Config config;
ConfigManager configManager;

// Default GPIOs for relay & switches (expandable)
const int defaultRelayPins[] = {15, 2, 4, 16, 17, 5, 18, 19};
const int defaultSwitchPins[] = {13, 12, 14, 27, 26, 25, 33, 32};
const int defaultZeroCrossPin = 34;
const int defaultAcs712Pin = 35;              // Example ADC pin for current sensing
const float defaultAcs712Sensitivity = 0.100; // 20A ACS712 (0.100 V/A)

String defaultName = "SUTORIT_" + String((uint32_t)ESP.getEfuseMac(), HEX).substring(4);

bool ConfigManager::load()
{
    if (!SPIFFS.begin(true))
        return false;

    if (!SPIFFS.exists("/config.json"))
    {
        Serial.println("[CONFIG] No config file found. Using defaults.");
        setDefaults();
        return false;
    }

    File file = SPIFFS.open("/config.json", "r");
    if (!file)
        return false;

    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, file);
    if (err)
    {
        Serial.println("[CONFIG] JSON parse error. Using defaults.");
        setDefaults();
        return false;
    }

    // Basic config fields
    config.device_name = doc["device_name"] | defaultName.c_str();
    config.device_password = doc["device_password"] | "password";
    config.wifi_ssid = doc["wifi_ssid"] | "";
    config.wifi_password = doc["wifi_password"] | "";
    config.mqtt_broker = doc["mqtt_broker"] | "";
    config.mqtt_port = doc["mqtt_port"] | 1883;
    config.mqtt_user = doc["mqtt_user"] | "";
    config.mqtt_pass = doc["mqtt_pass"] | "";
    config.firebase_url = doc["firebase_url"] | "";
    config.firebase_key = doc["firebase_key"] | "";
    config.mode = doc["mode"] | "master";
    config.master_ip = doc["master_ip"] | "";
    config.device_ip = doc["wifi_ip"] | "";
    config.gateway_ip = doc["wifi_ipgetway"] | "";
    config.subnetMask = doc["wifi_subnetmask"] | "255.255.255.0";
    config.dns = doc["wifi_dns"] | "8.8.8.8";
    config.OnlineStatus = doc["OnlineStatus"] | "Offline.";
    config.zeroCrossPin = doc["zero_cross_pin"] | defaultZeroCrossPin;
    config.acs712Pin = doc["acs712_pin"] | defaultAcs712Pin;
    config.acs712Sensitivity = doc["acs712_sensitivity"] | defaultAcs712Sensitivity;

    // JSON arrays
    JsonArray relays = doc.containsKey("relay_pins") ? doc["relay_pins"].as<JsonArray>() : JsonArray();
    JsonArray switches = doc.containsKey("switch_pins") ? doc["switch_pins"].as<JsonArray>() : JsonArray();
    JsonArray mapping = doc.containsKey("switch_to_output") ? doc["switch_to_output"].as<JsonArray>() : JsonArray();
    JsonArray types = doc.containsKey("switch_type") ? doc["switch_type"].as<JsonArray>() : JsonArray();
    JsonArray logic = doc.containsKey("switch_logic") ? doc["switch_logic"].as<JsonArray>() : JsonArray(); // NEW

    // Dynamic MAX_CHANNELS loop
    int maxDefault = sizeof(defaultRelayPins) / sizeof(defaultRelayPins[0]);
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        config.relay_pins[i] = (i < relays.size() && !relays[i].isNull()) ? relays[i].as<int>() : (i < maxDefault ? defaultRelayPins[i] : 0);
        config.switch_pins[i] = (i < switches.size() && !switches[i].isNull()) ? switches[i].as<int>() : (i < maxDefault ? defaultSwitchPins[i] : 0);
        config.switch_to_output[i] = (i < mapping.size() && !mapping[i].isNull()) ? mapping[i].as<int>() - 1 : i;
        config.switch_type[i] = (i < types.size() && !types[i].isNull()) ? types[i].as<String>() : "ac";      // default AC
        config.switch_logic[i] = (i < logic.size() && !logic[i].isNull()) ? logic[i].as<String>() : "toggle"; // NEW
    }

    generateDeviceID();
    return true;
}

bool ConfigManager::save()
{
    StaticJsonDocument<4096> doc;
    String devName = config.device_name;
    devName.toUpperCase();

    String cleaned = "";
    for (unsigned int i = 0; i < devName.length(); i++)
    {
        char c = devName[i];

        // remove space + all special characters
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        {
            cleaned += c;
        }
    }


    doc["device_name"] = cleaned;
    doc["wifi_ssid"] = config.wifi_ssid;
    doc["wifi_password"] = config.wifi_password;
    doc["device_password"] = config.device_password;
    doc["mqtt_broker"] = config.mqtt_broker;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = config.mqtt_user;
    doc["mqtt_pass"] = config.mqtt_pass;
    doc["firebase_url"] = config.firebase_url;
    doc["firebase_key"] = config.firebase_key;
    doc["mode"] = config.mode;
    doc["master_ip"] = config.master_ip;
    doc["wifi_ip"] = config.device_ip;
    doc["wifi_ipgetway"] = config.gateway_ip;
    doc["wifi_subnetmask"] = config.subnetMask;
    doc["wifi_dns"] = config.dns;
    doc["OnlineStatus"] = config.OnlineStatus;
    doc["zero_cross_pin"] = config.zeroCrossPin;
    doc["acs712_pin"] = config.acs712Pin;
    doc["acs712_sensitivity"] = config.acs712Sensitivity;

    // Create JSON arrays
    JsonArray relays = doc.createNestedArray("relay_pins");
    JsonArray switches = doc.createNestedArray("switch_pins");
    JsonArray mapping = doc.createNestedArray("switch_to_output");
    JsonArray types = doc.createNestedArray("switch_type");
    JsonArray logic = doc.createNestedArray("switch_logic"); // NEW

    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        relays.add(config.relay_pins[i]);
        switches.add(config.switch_pins[i]);
        mapping.add(config.switch_to_output[i]);
        types.add(config.switch_type[i]);
        logic.add(config.switch_logic[i]); // NEW
    }

    File file = SPIFFS.open("/config.json", "w");
    if (!file)
        return false;

    serializeJson(doc, file);
    return true;
}

bool ConfigManager::isConfigured()
{
    return !(config.wifi_ssid.isEmpty() || config.wifi_password.isEmpty());
}

void ConfigManager::setDefaults()
{
    config.device_name = defaultName.c_str();
    config.device_password = "password";
    config.wifi_ssid = "";
    config.wifi_password = "";
    config.mqtt_broker = "";
    config.mqtt_port = 1883;
    config.mqtt_user = "";
    config.mqtt_pass = "";
    config.firebase_url = "";
    config.firebase_key = "";
    config.mode = "master";
    config.master_ip = "";
    config.device_ip = "";
    config.gateway_ip = "";
    config.subnetMask = "255.255.255.0";
    config.dns = "8.8.8.8";
    config.OnlineStatus = "Offline.";
    config.zeroCrossPin = defaultZeroCrossPin;
    config.acs712Pin = defaultAcs712Pin;
    config.acs712Sensitivity = defaultAcs712Sensitivity;

    int maxDefault = sizeof(defaultRelayPins) / sizeof(defaultRelayPins[0]);
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        config.relay_pins[i] = (i < maxDefault) ? defaultRelayPins[i] : 0;
        config.switch_pins[i] = (i < maxDefault) ? defaultSwitchPins[i] : 0;
        config.switch_to_output[i] = i;
        config.switch_type[i] = "ac";      // default all AC
        config.switch_logic[i] = "toggle"; // NEW default
    }
    generateDeviceID();
}

void ConfigManager::generateDeviceID()
{
    uint64_t chipid = ESP.getEfuseMac();
    char macStr[13];
    sprintf(macStr, "%06llX", chipid & 0xFFFFFF); // last 3 bytes only
    config.device_id = config.device_name + "_" + String(macStr);
}

bool ConfigManager::factoryReset()
{
    Serial.println("[CONFIG] Performing factory reset...");

    if (SPIFFS.exists("/config.json"))
    {
        SPIFFS.remove("/config.json");
        Serial.println("[CONFIG] Deleted /config.json");
    }

    Serial.println("[CONFIG] Factory reset complete. Restarting...");
    delay(500);
    ESP.restart();
    return true; // will not reach here
}
