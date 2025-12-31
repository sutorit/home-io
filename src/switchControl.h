#ifndef SWITCHCONTROL_H
#define SWITCHCONTROL_H

#include <Arduino.h>
#include "configManager.h"

class SwitchControl
{
public:
  void begin();
  void setRelay(uint8_t ch, bool state, bool publish = true);
  void toggleRelay(uint8_t ch,bool publish = true); // modified to add publish parameter
  bool getState(uint8_t ch);
  void loop();
  void publishState(uint8_t ch);

  // NEW: set brightness (0–100%)
  void setBrightness(uint8_t ch, uint8_t value, bool publish = true);
  uint8_t getBrightness(uint8_t ch);

  // Called from network callbacks (ISR-like context): enqueue only, do not touch GPIO here.
  void enqueueCloudSet(uint8_t ch, bool state);

private:
  bool relayState[MAX_CHANNELS] = {false};
  uint8_t relayBrightness[MAX_CHANNELS] = {0};   // NEW: brightness per channel (0–100)
  bool scheduledOverride[MAX_CHANNELS] = {false}; 

  unsigned long lastDebounceTime[MAX_CHANNELS] = {0};
  bool lastSwitchReading[MAX_CHANNELS] = {HIGH}; // debounce raw read
  bool switchState[MAX_CHANNELS] = {HIGH};       // stable state
  const unsigned long debounceDelay = 50;        // ms

  // Cloud commands queue (processed in loop to avoid work in async_tcp thread)
  volatile bool cloudPending[MAX_CHANNELS] = {false};
  volatile bool cloudState[MAX_CHANNELS] = {false};

  // Throttle outgoing cloud writes (avoid bursts)
  unsigned long lastPublishMs[MAX_CHANNELS] = {0};
  const unsigned long minPublishIntervalMs = 120; // tune if needed
};

extern SwitchControl switchControl;
#endif
