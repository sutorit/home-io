#include "mqttHandler.h"
#include "configManager.h"
#include "switchControl.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

WiFiClient espClient;
PubSubClient client(espClient);
MqttHandler mqttHandler;

// ---------------- Internal State ----------------

unsigned long lastMqttReconnect = 0;
const unsigned long mqttReconnectInterval = 5000; // 5 sec retry

// ---------------- MQTT Callback ----------------
void callback(char *topic, byte *payload, unsigned int length)
{
    if (length >= 255)
        return;

    payload[length] = '\0';

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload))
        return;

    // Ignore self message
    String src = doc["source"] | "";
    if (src == config.device_id)
        return;

    // Extract channel from topic
    String topicStr(topic);
    int lastSlash = topicStr.lastIndexOf('/');
    if (lastSlash < 0)
        return;

    int ch = topicStr.substring(lastSlash + 1).toInt();
    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    if (doc.containsKey("value"))
    {
        int value = constrain(doc["value"], 0, 100);

        Serial.printf("[MQTT] RX CH%d -> %d\n", ch, value);

        // ONE SINGLE SOURCE OF TRUTH
        switchControl.setBrightness(ch, value, true); // this will update relay state, publish to MQTT/Firebase, and save config
    }
}
// ---------------- Begin ----------------
void MqttHandler::begin()
{
    client.setServer(config.mqtt_broker.c_str(), config.mqtt_port);
    client.setCallback(callback);

    client.setKeepAlive(15);
    client.setSocketTimeout(3); // Prevent long blocking
}

// ---------------- Safe Reconnect ----------------
void handleMQTT()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    if (client.connected())
        return;

    if (millis() - lastMqttReconnect < mqttReconnectInterval)
        return;

    lastMqttReconnect = millis();

    Serial.println("[MQTT] Attempt reconnect...");

    bool connected = client.connect(
        config.device_id.c_str(),
        config.mqtt_user.c_str(),
        config.mqtt_pass.c_str());

    if (connected)
    {
        String topic = config.device_id + "/sw/+";
        client.subscribe(topic.c_str());

        Serial.println("[MQTT] Reconnected OK");

        mqttHandler.publishAllRelayStates();
    }
    else
    {
        Serial.print("[MQTT] Failed, rc=");
        Serial.println(client.state());
    }
}

// ---------------- Loop ----------------
void MqttHandler::loop()
{
    handleMQTT();

    if (client.connected())
        client.loop();

    delay(1); // for ESP32 stability
}

// ---------------- Publish All ----------------
void MqttHandler::publishAllRelayStates()
{
    if (!client.connected())
        return;

    for (uint8_t ch = 1; ch <= MAX_CHANNELS; ch++)
    {
        int brightness = switchControl.getBrightness(ch);
        publishRelayState(ch, brightness);
        delay(2);
    }
}

// ---------------- Publish Single ----------------
void MqttHandler::publishRelayState(uint8_t ch, int value)
{
    if (!client.connected())
        return;
    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    value = constrain(value, 0, 100);

    StaticJsonDocument<128> doc;
    doc["ch"] = ch;
    doc["value"] = value;
    doc["source"] = config.device_id;

    char buffer[128];
    serializeJson(doc, buffer);

    String topic = config.device_id + "/sw/" + String(ch);

    client.publish(topic.c_str(), buffer, true);

    Serial.printf("[MQTT] Published CH%d -> %d\n", ch, value);
}

// ---------------- Stop ----------------
void MqttHandler::stop()
{
    if (client.connected())
        client.disconnect();
}

bool MqttHandler::isReady()
{
    return client.connected();
}