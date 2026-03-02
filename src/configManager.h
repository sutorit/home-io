#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <ArduinoJson.h>

// Allow flexible number of channels
#ifndef MAX_CHANNELS   // Default to 8 if not defined in configManager.cpp
#define MAX_CHANNELS 8 // Change this to 4, 6, or 8 etc.
#endif

struct Config
{
    String device_name;           // For AP SSID and mDNS
    String device_id;             // Unique ID for MQTT/Firebase topics (can be generated from device name + random suffix)
    String wifi_ssid;             // wifi SSID for STA mode
    String wifi_password;         // wifi password for STA mode
    String device_password;       // device password for AP mode and config page access
    String mqtt_broker;           // MQTT broker address
    int mqtt_port;                // MQTT broker port
    String mqtt_user;             // MQTT username
    String mqtt_pass;             // MQTT password
    String firebase_url;          // Firebase Realtime Database URL
    String firebase_key;          // Firebase database secret or API key
    String mode;                  // "master" or "slave" mode (for MQTT master/slave setups)
    String device_ip;             // Static IP for STA mode (optional)
    String master_ip;             // For MQTT master/slave mode (optional)
    String gateway_ip;            // For STA mode static IP configuration
    String subnetMask;            // For STA mode static IP configuration
    String dns;                   // For STA mode static IP configuration
    String OnlineStatus;          // "online" or "offline"
    String firmware_version;      // For OTA update checks
    String last_firmware_version; // To store the last known firmware version (for update notifications)
    String last_release_notes;    // To store release notes of the latest firmware (for update notifications)
    String last_update_time;      // To store the last time the device checked for updates (for update notifications)

    // Arrays depend on MAX_CHANNELS
    String switch_type[MAX_CHANNELS];   // "ac" or "dc"
    String switch_logic[MAX_CHANNELS];  // "toggle", "bell", "2way"
    int zeroCrossPin;                   // GPIO pin for zero-cross detection (for AC control)
    int relay_pins[MAX_CHANNELS];       // Physical output pins for each relay
    int switch_pins[MAX_CHANNELS];      // Physical input pins for each switch (for bell/2way logic)
    int switch_to_output[MAX_CHANNELS]; // Mapping from switch index to output index (0-based)

    bool output_state[MAX_CHANNELS]; // Current state of each output (for MQTT/Firebase status updates)
    bool mqtt_enable = false;        // Add more config fields as needed
    bool firebase_enable = false;    // Add more config fields as needed
    int acs712Pin;                   // ADC pin for ACS712 current sensor
    float acs712Sensitivity;         // V/A sensitivity (e.g. 0.100 for 20A module)
    int i2c_sda;                     // I2C SDA pin (if using I2C-based current sensor)
    int i2c_scl;                     // I2C SCL pin (if using I2C-based current sensor)
};

class ConfigManager
{
public:
    bool load();
    bool save();
    bool isConfigured();
    void setDefaults();
    void generateDeviceID();
    bool factoryReset();
};

extern Config config;
extern ConfigManager configManager;

#endif
