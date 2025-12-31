#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <ArduinoJson.h>

// Allow flexible number of channels
#ifndef MAX_CHANNELS
#define MAX_CHANNELS 8 // Change this to 4, 6, or 8 etc.
#endif

struct Config
{
    String device_name;
    String device_id;
    String wifi_ssid;
    String wifi_password;
    String device_password;
    String mqtt_broker;
    int mqtt_port;
    String mqtt_user;
    String mqtt_pass;
    String firebase_url;
    String firebase_key;
    String mode;
    String device_ip;
    String master_ip;
    String gateway_ip;
    String subnetMask;
    String dns;
    String OnlineStatus;

    // Arrays depend on MAX_CHANNELS
    String switch_type[MAX_CHANNELS];  // "ac" or "dc"
    String switch_logic[MAX_CHANNELS]; // "toggle", "bell", "2way"
    int zeroCrossPin;
    int relay_pins[MAX_CHANNELS];
    int switch_pins[MAX_CHANNELS];
    int switch_to_output[MAX_CHANNELS];
    int acs712Pin;           // ADC pin for ACS712 current sensor
    float acs712Sensitivity; // V/A sensitivity (e.g. 0.100 for 20A module)
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
