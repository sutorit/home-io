#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include "configManager.h"

#define MAX_SCHEDULES_PER_CHANNEL 6 // 6 schedules per channel

enum ScheduleRepeat
{
    REPEAT_ONCE = 0,
    REPEAT_DAILY = 1,
    REPEAT_WEEKLY = 2
};

struct ScheduleItem
{
    bool enabled = false;
    int repeat = 0;
    String time = "";       // "HH:MM"
    int scheduleValue = -1; // 0–100
    String scheduleId = "";
    bool executed = false;
    int lastExecDay = -1;
    bool recovered = false; // for missed-schedule recovery
    String firebaseKey;     // actual node key under /schedules/
};

class ScheduleManager
{
public:
    void updateFromFirebase(int ch, int index, FirebaseJson &json);
    void updateSchedulesFromFirebase(int ch, FirebaseJson &schedulesJson);

    void loop();

    // recovery support
    void recoverMissed();

private:
    ScheduleItem schedules[MAX_CHANNELS + 1][MAX_SCHEDULES_PER_CHANNEL];
    int scheduleCount[MAX_CHANNELS + 1] = {0};

    //  helper
    bool isMissed(ScheduleItem &s, struct tm &now);
};

extern ScheduleManager schedulemanager;

#endif
