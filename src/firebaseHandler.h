#ifndef FIREBASEHANDLER_H
#define FIREBASEHANDLER_H

#include <Arduino.h>
#include <Firebase_ESP_Client.h>

extern FirebaseData fbdo;
extern FirebaseData fbWrite;
extern FirebaseData fbStream;

class FirebaseHandler
{
public:
    void begin();
    void loop();
    void sendRelayState(uint8_t ch, int value = 0 );
    void sendRelayStateSchedule(uint8_t ch, int tempValue, bool scheduleActive, const String &time, bool repeat);
    void sendBootValueOnly(uint8_t ch);
    // Enable / disable schedule only
    void setScheduleEnabled(uint8_t ch,
                            const String &scheduleId,
                            bool enable);

    // Delete one schedule
    void deleteSchedule(uint8_t ch,
                        const String &scheduleId);
    void stop();
    bool isReady();
};

extern FirebaseHandler firebaseHandler;

#endif
