#ifndef USAGES_HANDLER_H
#define USAGES_HANDLER_H

#include <Arduino.h>

class UsageHandler
{
private:
    int sensorPin;
    float sensitivity; // V/A
    float offsetVoltage;
    float supplyVoltage;
    int zeroCrossPin;
    unsigned long lastEnergyCalc = 0;
    float totalEnergyWh = 0.0;
    float lastCurrent = 0.0;
    unsigned long lastRead = 0; 
    volatile bool zeroCrossDetected = false;

public:
    UsageHandler(int pin, float sens, int zcPin);

    void begin();
    void handleZeroCross();
    void loop();
    float readCurrentAC();
    float getRMSCurrent();
    float getPower(float voltageRMS = 230.0);
    float getEnergy(float voltageRMS = 230.0);
    float getLastCurrent();

    String getStatusJSON();
};

#endif
