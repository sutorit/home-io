#ifndef MQTTHANDLER_H
#define MQTTHANDLER_H

#include <Arduino.h>

class MqttHandler
{
public:
  void begin();
  void loop();
  // updated: now also supports brightness (0–100)
  void publishRelayState(uint8_t ch, int value);
  void publishState(uint8_t ch);
  void stop();
  void publishAllRelayStates();
  bool isReady();
private:
  void connect();
};

extern MqttHandler mqttHandler;

#endif
