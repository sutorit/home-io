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
    void sendRelayState(uint8_t ch, int value = 0 ); // value: 0-100 (brightness), 0 means OFF
    void sendRelayStateSchedule(uint8_t ch, int tempValue, bool scheduleActive, const String &time, bool repeat); // Updates the relay state and schedule fields for the given channel. If scheduleActive is false, only the "schedule" field is updated to 0 and time/repeat are cleared.
    void sendBootValueOnly(uint8_t ch);
    // Enable / disable schedule only
    void setScheduleEnabled(uint8_t ch,const String &scheduleId,bool enable); // Updates the "schedule" field for the given schedule ID to 1 (enabled) or 0 (disabled)

    // Delete one schedule
    void deleteSchedule(uint8_t ch,const String &scheduleId); // Deletes the schedule node with the given ID, if it exists
    void syncSwitchFromFirebase(uint8_t ch); // Syncs the switch state and brightness for the given channel from Firebase at boot. Does not force OFF if Firebase value is 0.
    void syncSchedulesFromFirebase(uint8_t ch); // Syncs the schedules for the given channel from Firebase at boot. Updates the schedule manager with the retrieved schedules.

    void stop();
    bool isReady();
};

extern FirebaseHandler firebaseHandler;

#endif
