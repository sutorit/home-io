#ifndef SERIAL_COMMUNICATION_H
#define SERIAL_COMMUNICATION_H

#include <Arduino.h>

class SerialCommunication
{
public:
    void begin(long baudRate = 115200, int rxPin = 16, int txPin = 17);
    void loop();

    // Called by SwitchControl when something changes
    void notifyRelayChange(uint8_t ch, bool state, uint8_t brightness);
    void notifyBrightnessChange(uint8_t ch, uint8_t value);

    void sendMessage(const String &msg);

private:
    void processIncomingCommand(const String &cmd, HardwareSerial &source);
    void handleCommand(const String &cmd);

    String usbBuffer;
    String uartBuffer;
};

extern SerialCommunication serialCom;

#endif
