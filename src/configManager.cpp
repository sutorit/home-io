#include "configManager.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h> // needed for WiFi.localIP()

Config config;
ConfigManager configManager;

#if CONFIG_IDF_TARGET_ESP32

// ===== ESP32 =====
const int defaultRelayPins[8] = {15, 2, 4, 16, 17, 5, 18, 19};
const int defaultSwitchPins[8] = {13, 12, 14, 27, 26, 25, 33, 32};
const int defaultZeroCrossPin = 34;
const int defaultAcs712Pin = 35;

#define DEFAULT_I2C_SDA 21
#define DEFAULT_I2C_SCL 22

const int LED_RED_PIN = 1;   // TX0
const int LED_GREEN_PIN = 3; // RX0

#elif CONFIG_IDF_TARGET_ESP32C6

// ===== ESP32-C6-WROOM-1 =====
const int defaultRelayPins[8] = {4, 5, 6, 7, 8, 9, 10, 11};
const int defaultSwitchPins[8] = {12, 13, 15, 16, 17, 18, 19, 20};

const int defaultZeroCrossPin = 2;
const int defaultAcs712Pin = 3;

#define DEFAULT_I2C_SDA 22
#define DEFAULT_I2C_SCL 23

const int LED_RED_PIN = 1;
const int LED_GREEN_PIN = 21;

#endif

const float defaultAcs712Sensitivity = 0.100; // 20A ACS712 (0.100 V/A)

String defaultName = "SUTORIT_" + String((uint32_t)ESP.getEfuseMac(), HEX).substring(4);

// Load config from SPIFFS, return true if successful
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
    config.device_name = doc["device_name"] | defaultName.c_str();                   // default to unique name based on MAC
    config.device_password = doc["device_password"] | "password";                    // default password (should be changed by user)
    config.firmware_version = doc["firmware_version"] | "V0.10.1.1";                 // default firmware version (should be updated by user)
    config.last_firmware_version = doc["last_firmware_version"] | "";                // default last firmware version (used for update checks, can be empty)
    config.last_release_notes = doc["last_release_notes"] | "";                      // default last release notes (used for update checks, can be empty)
    config.last_update_time = doc["last_update_time"] | "";                          // default last update time (used for update checks, can be empty)
    config.wifi_ssid = doc["wifi_ssid"] | "";                                        // default empty SSID (not configured)
    config.wifi_password = doc["wifi_password"] | "";                                // default empty WiFi password (not configured)
    config.mqtt_broker = doc["mqtt_broker"] | "";                                    // default empty MQTT broker (not configured)
    config.mqtt_port = doc["mqtt_port"] | 1883;                                      // default MQTT port
    config.mqtt_user = doc["mqtt_user"] | "";                                        // default empty MQTT user (not configured)
    config.mqtt_pass = doc["mqtt_pass"] | "";                                        // default empty MQTT password (not configured)
    config.firebase_url = doc["firebase_url"] | "";                                  // default empty Firebase URL (not configured)
    config.firebase_key = doc["firebase_key"] | "";                                  // default empty Firebase key (not configured)
    config.mqtt_enable = doc["mqtt_enable"] | false;                                 // default MQTT/Firebase disabled (can be enabled by user)
    config.firebase_enable = doc["firebase_enable"] | false;                         // default MQTT/Firebase disabled (can be enabled by user)
    config.mode = doc["mode"] | "master";                                            // default mode "master" (can be "master" or "slave")
    config.master_ip = doc["master_ip"] | "";                                        // default empty master IP (used if mode is "slave")
    config.device_ip = doc["wifi_ip"] | "";                                          // default empty device IP (can be set to static IP, or left empty for DHCP)
    config.gateway_ip = doc["wifi_ipgetway"] | "";                                   // default empty gateway IP (can be set to router IP, or left empty for DHCP)
    config.subnetMask = doc["wifi_subnetmask"] | "255.255.255.0";                    // default subnet mask (common default for home networks)
    config.dns = doc["wifi_dns"] | "8.8.8.8";                                        // default DNS (Google DNS)
    config.OnlineStatus = doc["OnlineStatus"] | "Offline.";                          // default online status (can be updated by device to "Online." when connected)
    config.zeroCrossPin = doc["zero_cross_pin"] | defaultZeroCrossPin;               // default zero cross pin
    config.acs712Pin = doc["acs712_pin"] | defaultAcs712Pin;                         // default ACS712 pin
    config.acs712Sensitivity = doc["acs712_sensitivity"] | defaultAcs712Sensitivity; // default ACS712 sensitivity
    config.i2c_sda = doc["i2c_sda"] | DEFAULT_I2C_SDA;                               // default I2C SDA pin
    config.i2c_scl = doc["i2c_scl"] | DEFAULT_I2C_SCL;                               // default I2C SCL pin

    // JSON arrays
    JsonArray relays = doc.containsKey("relay_pins") ? doc["relay_pins"].as<JsonArray>() : JsonArray();
    JsonArray switches = doc.containsKey("switch_pins") ? doc["switch_pins"].as<JsonArray>() : JsonArray();
    JsonArray mapping = doc.containsKey("switch_to_output") ? doc["switch_to_output"].as<JsonArray>() : JsonArray();
    JsonArray types = doc.containsKey("switch_type") ? doc["switch_type"].as<JsonArray>() : JsonArray();
    JsonArray logic = doc.containsKey("switch_logic") ? doc["switch_logic"].as<JsonArray>() : JsonArray();
    JsonArray states = doc.containsKey("output_state") ? doc["output_state"].as<JsonArray>() : JsonArray();

    // Dynamic MAX_CHANNELS loop
    int maxDefault = sizeof(defaultRelayPins) / sizeof(defaultRelayPins[0]);
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        config.relay_pins[i] = (i < relays.size() && !relays[i].isNull()) ? relays[i].as<int>() : (i < maxDefault ? defaultRelayPins[i] : 0);         // default pins or 0
        config.switch_pins[i] = (i < switches.size() && !switches[i].isNull()) ? switches[i].as<int>() : (i < maxDefault ? defaultSwitchPins[i] : 0); // default pins or 0
        config.switch_to_output[i] = (i < mapping.size() && !mapping[i].isNull()) ? mapping[i].as<int>() - 1 : i;                                     // default 1:1 mapping, convert 1-based to 0-based
        config.switch_type[i] = (i < types.size() && !types[i].isNull()) ? types[i].as<String>() : "ac";                                              // default all switch AC
        config.switch_logic[i] = (i < logic.size() && !logic[i].isNull()) ? logic[i].as<String>() : "push";                                           // default all switch push
        config.output_state[i] = (i < states.size() && !states[i].isNull()) ? states[i].as<bool>() : false;                                           // default OFF
    }

    generateDeviceID();
    return true;
}

// Save config to SPIFFS, return true if successful
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

    doc["device_name"] = cleaned; // save cleaned name to ensure it's valid on next load
    doc["wifi_ssid"] = config.wifi_ssid;
    doc["wifi_password"] = config.wifi_password;
    doc["device_password"] = config.device_password;
    doc["firmware_version"] = config.firmware_version;
    doc["last_firmware_version"] = config.last_firmware_version;
    doc["last_release_notes"] = config.last_release_notes;
    doc["last_update_time"] = config.last_update_time;
    doc["mqtt_broker"] = config.mqtt_broker;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_user"] = config.mqtt_user;
    doc["mqtt_pass"] = config.mqtt_pass;
    doc["firebase_url"] = config.firebase_url;
    doc["firebase_key"] = config.firebase_key;
    doc["mqtt_enable"] = config.mqtt_enable;
    doc["firebase_enable"] = config.firebase_enable;
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
    doc["i2c_sda"] = config.i2c_sda;
    doc["i2c_scl"] = config.i2c_scl;

    // Create JSON arrays
    JsonArray relays = doc.createNestedArray("relay_pins");
    JsonArray switches = doc.createNestedArray("switch_pins");
    JsonArray mapping = doc.createNestedArray("switch_to_output");
    JsonArray types = doc.createNestedArray("switch_type");
    JsonArray logic = doc.createNestedArray("switch_logic");
    JsonArray states = doc.createNestedArray("output_state");

    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        relays.add(config.relay_pins[i]);
        switches.add(config.switch_pins[i]);
        mapping.add(config.switch_to_output[i] + 1); // save as 1-based for easier user editing (0 can be confusing in JSON)
        types.add(config.switch_type[i]);
        logic.add(config.switch_logic[i]);
        states.add(config.output_state[i]);
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

// Set default values for all config fields
void ConfigManager::setDefaults()
{
    config.device_name = defaultName.c_str(); // default unique name based on MAC (e.g. SUTORIT_123456)
    config.device_password = "password";      // default password (should be changed by user)
    config.firmware_version = "V0.10.1.1";    // default firmware version (should be updated by user)
    config.last_firmware_version = "";        // default last firmware version (used for update checks, can be empty)
    config.last_release_notes = "";
    config.last_update_time = "";
    config.wifi_ssid = "";                               // default empty SSID (not configured)
    config.wifi_password = "";                           // default empty WiFi password (not configured)
    config.mqtt_broker = "";                             // default empty MQTT broker (not configured)
    config.mqtt_port = 1883;                             // default MQTT port
    config.mqtt_user = "";                               // default empty MQTT user (not configured)
    config.mqtt_pass = "";                               // default empty MQTT password (not configured)
    config.firebase_url = "";                            // default empty Firebase URL (not configured)
    config.firebase_key = "";                            // default empty Firebase key (not configured)
    config.mqtt_enable = false;                          // default MQTT/Firebase disabled (can be enabled by user)
    config.firebase_enable = false;                      // default MQTT/Firebase disabled (can be enabled by user)
    config.mode = "master";                              // default mode "master" (can be "master" or "slave")
    config.master_ip = "";                               // default empty master IP (used if mode is "slave")
    config.device_ip = "";                               // default empty device IP (can be set to static IP, or left empty for DHCP)
    config.gateway_ip = "";                              // default empty gateway IP (can be set to router IP, or left empty for DHCP)
    config.subnetMask = "255.255.255.0";                 // default subnet mask (common default for home networks)
    config.dns = "8.8.8.8";                              // default DNS (Google DNS)
    config.OnlineStatus = "Offline.";                    // default online status (can be updated by device to "Online." when connected)
    config.zeroCrossPin = defaultZeroCrossPin;           // default zero cross pin
    config.acs712Pin = defaultAcs712Pin;                 // default ACS712 pin
    config.acs712Sensitivity = defaultAcs712Sensitivity; // default ACS712 sensitivity
    config.i2c_sda = DEFAULT_I2C_SDA;                    // default I2C SDA pin
    config.i2c_scl = DEFAULT_I2C_SCL;                    // default I2C SCL pin

    int maxDefault = sizeof(defaultRelayPins) / sizeof(defaultRelayPins[0]);
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        config.relay_pins[i] = (i < maxDefault) ? defaultRelayPins[i] : 0;
        config.switch_pins[i] = (i < maxDefault) ? defaultSwitchPins[i] : 0;
        config.switch_to_output[i] = i;
        config.switch_type[i] = "ac";    // default all AC
        config.switch_logic[i] = "push"; // NEW default
        config.output_state[i] = false;
    }
    generateDeviceID();
}

// Generate device ID based on device name and last 3 bytes of MAC address
void ConfigManager::generateDeviceID()
{
    uint64_t chipid = ESP.getEfuseMac();
    char macStr[13];
    sprintf(macStr, "%06llX", chipid & 0xFFFFFF); // last 3 bytes only
    config.device_id = config.device_name + "_" + String(macStr);
}

// Factory reset: delete config file and restart device
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
