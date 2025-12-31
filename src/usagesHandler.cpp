#include "usagesHandler.h"

UsageHandler::UsageHandler(int pin, float sens, int zcPin)
{
    sensorPin = pin;
    sensitivity = sens;
    zeroCrossPin = zcPin;
    supplyVoltage = 3.3; // Change to 5.0 if using 5V reference
    offsetVoltage = supplyVoltage / 2.0;
}

void IRAM_ATTR zeroCrossISR(void *arg)
{
    UsageHandler *instance = static_cast<UsageHandler *>(arg);
    instance->handleZeroCross();
}

void UsageHandler::begin()
{
    pinMode(sensorPin, INPUT);
    pinMode(zeroCrossPin, INPUT_PULLUP);
    attachInterruptArg(digitalPinToInterrupt(zeroCrossPin), zeroCrossISR, this, RISING);
    Serial.println("[ACS712] Initialized with zero-cross detection");
}

void UsageHandler::handleZeroCross()
{
    zeroCrossDetected = true;
}

float UsageHandler::readCurrentAC()
{
    const int samples = 1000;
    float sum = 0.0;

    for (int i = 0; i < samples; i++)
    {
        int adcValue = analogRead(sensorPin);
        float voltage = (adcValue / 4095.0) * supplyVoltage;
        float current = (voltage - offsetVoltage) / sensitivity;
        sum += sq(current);
        delayMicroseconds(200);
    }

    float rmsCurrent = sqrt(sum / samples);
    lastCurrent = rmsCurrent;
    return rmsCurrent;
}

float UsageHandler::getRMSCurrent()
{
    return lastCurrent;
}

float UsageHandler::getPower(float voltageRMS)
{
    return voltageRMS * getRMSCurrent();
}

float UsageHandler::getEnergy(float voltageRMS)
{
    unsigned long now = millis();
    float elapsedHours = (now - lastEnergyCalc) / 3600000.0;

    float powerW = getPower(voltageRMS);
    totalEnergyWh += powerW * elapsedHours;
    lastEnergyCalc = now;

    return totalEnergyWh;
}

float UsageHandler::getLastCurrent()
{
    return lastCurrent;
}

String UsageHandler::getStatusJSON()
{
    float current = getRMSCurrent();
    float power = getPower();
    float energy = totalEnergyWh;

    String json = "{";
    json += "\"current\":" + String(current, 3) + ",";
    json += "\"power\":" + String(power, 1) + ",";
    json += "\"energy_wh\":" + String(energy, 2);
    json += "}";

    return json;
}

void UsageHandler::loop()
{
    if (millis() - lastRead > 2000)
    {
        readCurrentAC(); // update current reading
        lastRead = millis();
    }
}
