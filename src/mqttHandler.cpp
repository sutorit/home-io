#include "mqttHandler.h"
#include "configManager.h"
#include "switchControl.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

WiFiClient espClient;
PubSubClient client(espClient);
MqttHandler mqttHandler;

// ---------------- Global flags ----------------
volatile bool mqttRelayPending = false;
int mqttRelayCh = 0;
bool mqttRelayState = false;
volatile bool mqttBrightnessPending = false;
int mqttBrightnessCh = 0;
int mqttBrightnessVal = 0;

// ---------------- MQTT callback ----------------
void callback(char *topic, byte *payload, unsigned int length)
{
    payload[length] = '\0';
    String msg = String((char *)payload);

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (error)
    {
        Serial.println("[MQTT] Failed to parse JSON");
        return;
    }

    // Ignore self-published messages
    String src = doc["source"] | "";
    if (src == config.device_id)
        return;

    int ch = doc["ch"] | -1;

    // If JSON doesn't include ch, extract from topic
    if (ch < 1 || ch > MAX_CHANNELS)
    {
        String topicStr(topic);
        int lastSlash = topicStr.lastIndexOf('/');
        if (lastSlash >= 0)
            ch = topicStr.substring(lastSlash + 1).toInt();
    }

    if (ch < 1 || ch > MAX_CHANNELS)
    {
        Serial.printf("[MQTT] Invalid channel from topic '%s'\n", topic);
        return;
    }

    // --- Handle relay ---
    if (doc.containsKey("state"))
    {
        String state = doc["state"].as<String>();
        Serial.printf("[MQTT] Channel %d -> state: %s (queued)\n", ch, state.c_str());

        mqttRelayPending = true;
        mqttRelayCh = ch;
        mqttRelayState = (state == "on");
    }

    // --- Handle brightness (separate queue) ---
    if (doc.containsKey("brightness") && !doc.containsKey("state"))
    {
        int brightness = constrain(doc["brightness"], 0, 100);
        Serial.printf("[MQTT] Channel %d -> brightness: %d (queued)\n", ch, brightness);

        mqttBrightnessPending = true;
        mqttBrightnessCh = ch;
        mqttBrightnessVal = brightness;
    }
}

// ---------------- MQTT handler ----------------
void MqttHandler::begin()
{
    client.setServer(config.mqtt_broker.c_str(), config.mqtt_port);
    client.setCallback(callback);
}

void MqttHandler::connect()
{
    while (!client.connected())
    {
        Serial.print("[MQTT] Connecting...");
        if (client.connect(config.device_id.c_str(), config.mqtt_user.c_str(), config.mqtt_pass.c_str()))
        {
            Serial.println("connected");

            // Subscribe for all channels with wildcard
            String topic = "sutorit/" + config.device_id + "/relay/+";
            client.subscribe(topic.c_str());
            Serial.print("[MQTT] Subscribed: ");
            Serial.println(topic);

            // Publish all channels at startup
            publishAllRelayStates();
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            delay(3000);
        }
    }
}

void MqttHandler::loop()
{
    if (!client.connected())
        connect();
    client.loop();

    // Process queued MQTT commands safely outside the callback
    if (mqttRelayPending)
    {
        mqttRelayPending = false;
        switchControl.setRelay(mqttRelayCh, mqttRelayState);
    }

    if (mqttBrightnessPending)
    {
        mqttBrightnessPending = false;
        switchControl.setBrightness(mqttBrightnessCh, mqttBrightnessVal);
    }

    // Yield to prevent watchdog timeout
    delay(5);
}

// ---------------- Publish all relay states ----------------
void MqttHandler::publishAllRelayStates()
{
    for (uint8_t ch = 1; ch <= MAX_CHANNELS; ch++)
    {
        bool state = switchControl.getState(ch);          // Getter from switchControl
        int brightness = switchControl.getBrightness(ch); // Getter from switchControl
        publishRelayState(ch, state, brightness);
        delay(50);
    }
}

// ---------------- Publish single relay state ----------------
void MqttHandler::publishRelayState(uint8_t ch, bool state, int brightness)
{
    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    StaticJsonDocument<128> doc;
    doc["ch"] = ch;
    doc["state"] = state ? "on" : "off";

    if (brightness >= 0 && brightness <= 100)
        doc["brightness"] = brightness;

    doc["source"] = config.device_id;

    char buffer[128];
    serializeJson(doc, buffer);

    String topic = "sutorit/" + config.device_id + "/relay/" + String(ch);

    // Only debug on Serial
    Serial.print("[MQTT] Publishing topic: ");
    Serial.print(topic);
    Serial.print(" | Payload: ");
    Serial.println(buffer);

    client.publish(topic.c_str(), buffer, true); // retain = true
}

// ---------------- Stop MQTT ----------------
void MqttHandler::stop()
{
    if (client.connected())
        client.disconnect();

    Serial.println("[MQTT] Stopped");
}

bool MqttHandler::isReady()
{
    return client.connected();
}