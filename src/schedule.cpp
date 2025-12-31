#include "schedule.h"
#include "switchControl.h"
#include "firebaseHandler.h"
#include "configManager.h"
#include <time.h>

ScheduleManager schedulemanager;

void ScheduleManager::updateFromFirebase(int ch, FirebaseJson &json)
{
    FirebaseJsonData dTest;
    // Ignore value-only updates ONLY when schedule is disabled
    if (json.get(dTest, "value") &&
        !json.get(dTest, "time") &&
        !json.get(dTest, "repeat") &&
        !json.get(dTest, "schedule_value") &&
        schedules[ch].enabled)
    {

        Serial.printf("[SCH IGNORE] CH%d value-only (no schedule)\n", ch);
        return;
    }

    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    FirebaseJsonData d;
    ScheduleItem &s = schedules[ch];

    bool scheduleChanged = false;

    Serial.printf("\n[SCH RX] Firebase update for CH%d\n", ch);

    // -------- scheduleId --------
    if (json.get(d, "id")) // 🔥 FIXED
    {
        if (s.scheduleId != d.stringValue)
        {
            s.scheduleId = d.stringValue;
            scheduleChanged = true;
        }
    }

    // Apply value-only updates when schedule is active
    if (json.get(d, "value"))
    {
        switchControl.setRelay(ch, d.intValue > 0, false);
        switchControl.setBrightness(ch, d.intValue, false);

        Serial.printf("[SCH APPLY VALUE] CH%d -> %d%%\n", ch, d.intValue);
    }

    // -------- schedule enable --------
    if (json.get(d, "enable"))
    {
        bool en = d.intValue == 1;
        if (s.enabled != en)
        {
            s.enabled = en;
            scheduleChanged = true;
        }
    }

    // -------- scheduleId --------
    if (json.get(d, "scheduleId"))
    {
        if (s.scheduleId != d.stringValue)
        {
            s.scheduleId = d.stringValue;
            scheduleChanged = true;
        }
    }

    // -------- repeat --------
    if (json.get(d, "repeat"))
    {
        if (s.repeat != d.intValue)
        {
            s.repeat = d.intValue;
            scheduleChanged = true;
        }
    }

    // -------- time --------
    if (json.get(d, "time"))
    {
        if (s.time != d.stringValue)
        {
            s.time = d.stringValue;
            scheduleChanged = true;
        }
    }

    // -------- value (NO execution reset) --------
    if (json.get(d, "schedule_value"))
    {
        s.scheduleValue = d.intValue;
    }
    else if (json.get(d, "value"))
    {
        s.scheduleValue = d.intValue;
    }

    // -------- reset execution ONLY if schedule changed --------
    if (scheduleChanged)
    {
        s.executed = false;
        s.lastExecDay = -1;
        s.recovered = false; // ← IMPORTANT
        Serial.printf("[SCH RESET] CH%d execution reset\n", ch);
    }

    Serial.printf(
        "[SCH SYNC] CH%d | scId=%s enabled=%d repeat=%d time=%s value=%d\n",
        ch, s.scheduleId.c_str(), s.enabled, s.repeat, s.time.c_str(), s.scheduleValue);
}

bool deleteFirebasePath(const String &path)
{
    if (!Firebase.ready())
        return false;

    if (!Firebase.RTDB.deleteNode(&fbdo, path.c_str()))
    {
        Serial.printf("[FB DELETE FAIL]\nPath: %s\nCode: %d\nReason: %s\n",
                      path.c_str(),
                      fbdo.httpCode(),
                      fbdo.errorReason().c_str());
        return false;
    }

    Serial.printf("[FB DELETE OK] %s\n", path.c_str());
    return true;
}

bool ScheduleManager::isMissed(ScheduleItem &s, struct tm &now)
{
    if (!s.enabled || s.time.length() != 5)
        return false;

    int sh = s.time.substring(0, 2).toInt();
    int sm = s.time.substring(3, 5).toInt();

    int schedMin = sh * 60 + sm;
    int nowMin = now.tm_hour * 60 + now.tm_min;

    return nowMin > schedMin;
}

void ScheduleManager::updateSchedulesFromFirebase(int ch, FirebaseJson &schedulesJson)
{
    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    FirebaseJsonData d;
    size_t count = schedulesJson.iteratorBegin();

    Serial.printf("\n[SCH MULTI RX] CH%d schedules=%d\n", ch, count);

    bool applied = false;

    for (size_t i = 0; i < count; i++)
    {
        int type;
        String key;
        String raw; // ← raw JSON text

        schedulesJson.iteratorGet(i, type, key, raw);

        if (type != FirebaseJson::JSON_OBJECT)
            continue;

        // Parse child schedule object
        FirebaseJson oneSchedule;
        oneSchedule.setJsonData(raw);

        // Check enable
        if (!oneSchedule.get(d, "enable") || d.intValue != 1)
            continue;

        // ✅ APPLY FIRST ENABLED SCHEDULE
        schedules[ch].firebaseKey = key; // 🔥 STORE REAL KEY
        updateFromFirebase(ch, oneSchedule);

        applied = true;

        Serial.printf("[SCH ACTIVE] CH%d -> %s\n", ch, key.c_str());
        break; // 🔥 ONLY ONE ACTIVE SCHEDULE
    }

    schedulesJson.iteratorEnd();

    if (!applied)
    {
        schedules[ch].enabled = false;
        Serial.printf("[SCH NONE] CH%d no active schedules\n", ch);
    }
}

void ScheduleManager::recoverMissed()
{
    time_t nowT = time(nullptr);
    struct tm now;
    localtime_r(&nowT, &now);

    Serial.println("\n[SCH RECOVERY] Checking missed schedules...");

    for (int ch = 1; ch <= MAX_CHANNELS; ch++)
    {
        ScheduleItem &s = schedules[ch];

        if (!s.enabled || s.recovered)
            continue;

        if (!isMissed(s, now))
            continue;

        // ---------- ONCE ----------
        if (s.repeat == REPEAT_ONCE && !s.executed)
        {
            Serial.printf("[MISS] ONCE CH%d\n", ch);
        }
        // ---------- DAILY ----------
        else if (s.repeat == REPEAT_DAILY && s.lastExecDay != now.tm_yday)
        {
            Serial.printf("[MISS] DAILY CH%d\n", ch);
        }
        // ---------- WEEKLY ----------
        else if (s.repeat == REPEAT_WEEKLY &&
                 (now.tm_yday - s.lastExecDay) >= 7)
        {
            Serial.printf("[MISS] WEEKLY CH%d\n", ch);
        }
        else
        {
            s.recovered = true;
            continue;
        }

        // FIRE MISSED SCHEDULE
        Serial.printf("[SCH RECOVER FIRE] CH%d -> %d%% \n",
                      ch, s.scheduleValue);

        switchControl.setRelay(ch, s.scheduleValue > 0, false);
        switchControl.setBrightness(ch, s.scheduleValue, false);

        s.executed = true;
        s.lastExecDay = now.tm_yday;
        s.recovered = true;

        Firebase.RTDB.setInt(
            &fbdo,
            "/Home/" + config.device_id +
                "/switches/sw" + String(ch) + "/value",
            s.scheduleValue);

        // Clear ONCE
        if (s.repeat == REPEAT_ONCE && s.firebaseKey.length())
        {
            String path =
                "/Home/" + config.device_id +
                "/switches/sw" + String(ch) +
                "/schedules/" + s.firebaseKey;

            if (deleteFirebasePath(path))
            {
                s.enabled = false;
                s.executed = true;
                s.firebaseKey = "";
                s.scheduleId = "";
                s.recovered = true;

                Serial.printf("[SCH CLEAR] CH%d ONCE deleted\n", ch);
            }
        }
    }
}

void ScheduleManager::loop()
{
    // NTP not ready
    if (time(nullptr) < 100000)
        return;

    static int lastMinute = -1;

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    if (lastMinute == t.tm_min)
        return;
    lastMinute = t.tm_min;

    char nowStr[6];
    sprintf(nowStr, "%02d:%02d", t.tm_hour, t.tm_min);

    // Serial.printf("\n[TIME] %s | Day=%d\n", nowStr, t.tm_yday);

    for (int ch = 1; ch <= MAX_CHANNELS; ch++)
    {
        ScheduleItem &s = schedules[ch];

        // Serial.printf("[CHK] CH%d en=%d time=%s val=%d exec=%d\n",
        //               ch, s.enabled, s.time.c_str(),
        //               s.scheduleValue, s.executed);

        if (!s.enabled)
            continue;
        if (s.time.length() != 5)
            continue;
        if (s.scheduleValue < 0)
            continue;

        // ONCE
        if (s.repeat == REPEAT_ONCE && s.executed)
        {
            Serial.printf("[SKIP] CH%d already executed (ONCE)\n", ch);
            continue;
        }

        // DAILY / WEEKLY
        if ((s.repeat == REPEAT_DAILY || s.repeat == REPEAT_WEEKLY) &&
            s.executed && s.lastExecDay == t.tm_yday)
        {
            Serial.printf("[SKIP] CH%d already ran today\n", ch);
            continue;
        }

        if (String(nowStr) == s.time)
        {
            Serial.printf("\n[SCH FIRE] CH%d -> %d%% \n",
                          ch, s.scheduleValue);

            // Apply hardware
            switchControl.setRelay(ch, s.scheduleValue > 0, false);
            switchControl.setBrightness(ch, s.scheduleValue, false);

            // Lock execution BEFORE Firebase
            s.executed = true;
            s.lastExecDay = t.tm_yday;

            // Write ONLY value (NO schedule fields)
            Firebase.RTDB.setInt(
                &fbdo,
                "/Home/" + config.device_id +
                    "/switches/sw" + String(ch) + "/value",
                s.scheduleValue);

            Serial.printf("[SCH FB] CH%d value written\n", ch);

            // Cleanup ONCE
            if (s.repeat == REPEAT_ONCE && s.firebaseKey.length())
            {
                String path =
                    "/Home/" + config.device_id +
                    "/switches/sw" + String(ch) +
                    "/schedules/" + s.firebaseKey;

                if (deleteFirebasePath(path))
                {
                    s.enabled = false;
                    s.executed = true;
                    s.firebaseKey = "";
                    s.scheduleId = "";
                    s.recovered = true;

                    Serial.printf("[SCH CLEAR] CH%d ONCE deleted\n", ch);
                }
            }
        }
    }
}
