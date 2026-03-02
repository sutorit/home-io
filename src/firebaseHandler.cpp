#include "firebaseHandler.h"
#include "configManager.h"
#include "switchControl.h"
#include "schedule.h"
#include <Firebase_ESP_Client.h>
#include <WiFi.h>

// Firebase globals
FirebaseData fbStream; // for listening
FirebaseData fbWrite;  // for sending
FirebaseData fbdo;     // general purpose
FirebaseAuth auth;
FirebaseConfig fbConfig;

FirebaseHandler firebaseHandler;

#define GMT_OFFSET_SEC 19800 // +5:30
#define DAYLIGHT_OFFSET_SEC 0

bool bootSyncDone = false;

void FirebaseHandler::syncSwitchFromFirebase(uint8_t ch)
{
    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    int idx = ch - 1;

    String path = "/Home/" + config.device_id +
                  "/switches/sw" + String(ch) + "/value";

    if (!Firebase.RTDB.getInt(&fbdo, path.c_str()))
    {
        // Serial.printf("[BOOT SYNC] CH%d skipped (no Firebase value)\n", ch);
        return;
    }

    int fbValue = constrain(fbdo.intData(), 0, 100);

    // CRITICAL RULE:
    // Never force OFF at boot
    if (fbValue == 0)
    {
        // Serial.printf(
        //     "[BOOT SYNC] CH%d Firebase=0 → ignored (local=%s)\n",
        //     ch,
        //     config.output_state[idx] ? "ON" : "OFF");
        return;
    }

    // Only apply brightness if relay is already ON locally
    if (config.output_state[idx] || switchControl.getState(ch))
    {
        // switchControl.setRelay(ch, fbValue > 0, false);
        switchControl.setBrightness(ch, fbValue, false); // this will update relay state but NOT publish to Firebase again (avoid loop)

        Serial.printf(
            "[BOOT SYNC] CH%d brightness restored → %d%%\n",
            ch, fbValue);
    }
    else
    {
        Serial.printf(
            "[BOOT SYNC] CH%d skipped (relay OFF locally)\n",
            ch);
    }
}

void FirebaseHandler::syncSchedulesFromFirebase(uint8_t ch)
{
    String schedPath =
        "/Home/" + config.device_id +
        "/switches/sw" + String(ch) + "/schedules";

    if (!Firebase.RTDB.getJSON(&fbdo, schedPath.c_str()))
    {
        // Serial.printf("[BOOT SCH SYNC] CH%d no schedules\n", ch);
        return;
    }

    FirebaseJson &schedJson = fbdo.jsonObject();   // ✅ REFERENCE, not copy

    schedulemanager.updateSchedulesFromFirebase(ch, schedJson);

    // Serial.printf("[BOOT SCH SYNC] CH%d schedules loaded\n", ch);
}

void streamCallback(FirebaseStream data)
{
    if (!bootSyncDone)
    {
        Serial.println("[Firebase] Ignoring stream during boot sync");
        return;
    }

    String path = data.dataPath();
    Serial.printf("[Firebase] Stream update at path: %s\n", path.c_str());

    // -------- DEVICE ONLINE STATUS --------
    if (path.indexOf("/_status/online") >= 0)
    {
        int status = data.intData();
        if (status == 0)
        {
            Firebase.RTDB.setInt(
                &fbdo,
                "/Home/" + config.device_id + "/_status/online",
                1);
            Serial.println("[Firebase] Device back online");
        }
        return;
    }

    // -------- SWITCH NODE CHANGES --------
    if (path.indexOf("/switches/sw") < 0)
        return;

    // Extract channel number SAFELY
    int swIndex = path.lastIndexOf("sw");
    if (swIndex < 0)
        return;

    int ch = path.substring(swIndex + 2).toInt();
    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    // -------- FAST PATH: VALUE ONLY --------
    if (path.endsWith("/value"))
    {
        int value = data.intData();
        value = constrain(value, 0, 100);

        // switchControl.setRelay(ch, value > 0, false); // value > 0 means ON, else OFF
        switchControl.setBrightness(ch, value, false); // this will update relay state but NOT publish to Firebase again (avoid loop)

        Serial.printf("[Firebase] Relay CH%d -> %d%%\n", ch, value);
        return;
    }

    // -------- SCHEDULE TREE --------
    if (path.indexOf("/schedules") >= 0)
    {
        String schedPath =
            "/Home/" + config.device_id +
            "/switches/sw" + String(ch) + "/schedules";

        FirebaseJson schedJson;
        delay(80);

        if (Firebase.RTDB.getJSON(&fbdo, schedPath.c_str()))
        {
            schedJson = fbdo.jsonObject();
            schedulemanager.updateSchedulesFromFirebase(ch, schedJson);
            Serial.printf("[Firebase] CH%d schedules synced\n", ch);
        }
        return;
    }
    // else
    // {
    //     Serial.printf("[Firebase] Failed to sync CH%d -> %s\n",
    //                   ch, fbdo.errorReason().c_str());
    // }
}

// Timeout callback: called if stream times out
void streamTimeoutCallback(bool timeout)
{
    if (timeout)
    {
        Serial.println("[Firebase] Stream timeout, resuming...");
    }
}

// Initialize Firebase and start stream
void FirebaseHandler::begin()
{
    Serial.println("[Firebase] Initializing...");

    // --- 1️⃣ Ensure NTP time is synced before TLS ---
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
               "pool.ntp.org", "time.nist.gov");

    Serial.print("[Time] Syncing NTP");
    while (time(nullptr) < 100000)
    {
        Serial.print(".");
        delay(500);
    }
    Serial.println("\n[Time] NTP synced.");

    // --- 2️⃣ Configure Firebase ---
    fbConfig.database_url = config.firebase_url.c_str();
    fbConfig.signer.tokens.legacy_token = config.firebase_key.c_str();

    // --- 3️⃣ Optimize memory & connection behavior ---
    fbConfig.timeout.serverResponse = 10 * 1000;
    fbConfig.fcs.download_buffer_size = 2048;
    fbConfig.max_token_generation_retry = 3;
    fbConfig.cert.data = nullptr;
    Firebase.reconnectWiFi(true);

    Serial.printf("[Heap before Firebase] %lu bytes free\n", ESP.getFreeHeap());

    // --- 4️⃣ Begin Firebase connection ---
    Firebase.begin(&fbConfig, &auth);

    // ✅ WAIT until Firebase is fully ready
    Serial.print("[Firebase] Waiting for ready");
    while (!Firebase.ready())
    {
        Serial.print(".");
        delay(200);
    }
    Serial.println(" OK");

    // --- 5️⃣ BOOT SYNC FIRST (NO STREAM YET) ---
    Serial.println("[Firebase] Restoring last switch states...");
    for (int i = 1; i <= MAX_CHANNELS; i++)
    {
        delay(200);
        syncSwitchFromFirebase(i);    // Sync switch state first (fast path)
        syncSchedulesFromFirebase(i); // Then sync schedules (slower, but less critical for boot)
    }

    bootSyncDone = true;
    schedulemanager.recoverMissed();  // Check for any missed schedules during boot
    Serial.println("[Firebase] Boot sync completed");

    // --- 6️⃣ START RTDB STREAM AFTER BOOT SYNC ---
    String streamPath = "/Home/" + config.device_id;
    if (!Firebase.RTDB.beginStream(&fbStream, streamPath.c_str()))
    {
        Serial.printf("[Firebase] Stream begin error: %s\n",
                      fbStream.errorReason().c_str());
    }
    else
    {
        Firebase.RTDB.setStreamCallback(
            &fbStream,
            streamCallback,
            streamTimeoutCallback);

        Serial.println("[Firebase] Stream listening at " + streamPath);
    }

    // --- 7️⃣ Send device info ---
    String infoPath = "/Home/" + config.device_id + "/info";
    Firebase.RTDB.setString(&fbdo, infoPath + "/device_name", config.device_name);
    Firebase.RTDB.setString(&fbdo, infoPath + "/device_id", config.device_id);
    Firebase.RTDB.setString(&fbdo, infoPath + "/device_pass", config.device_password);

    // --- 8️⃣ Mark device online ---
    String statusPath = "/Home/" + config.device_id + "/_status";
    Firebase.RTDB.setInt(&fbdo, statusPath + "/online", 1);

    Serial.println("[Firebase] Initialization complete.");
}

// void FirebaseHandler::sendBootValueOnly(uint8_t ch)
// {
//     String path = "/Home/" + config.device_id + "/switches/sw" + String(ch);

//     FirebaseJson json;
//     json.set("value", 0);

//     Firebase.RTDB.updateNode(&fbWrite, path.c_str(), &json);
// }

// Definition in firebaseHandler.cpp
void FirebaseHandler::sendRelayState(uint8_t ch, int value)
{
    String path = "/Home/" + config.device_id + "/switches/sw" + String(ch);

    // Build JSON for the switch
    FirebaseJson json;
    json.set("value", constrain(value, 0, 100));

    // Debug log
    Serial.printf("[DEBUG] SW%d -> Value: %d\n",
                  ch, value);

    // Write to Firebase
    if (!Firebase.RTDB.updateNode(&fbWrite, path.c_str(), &json))
    {
        Serial.printf("[Firebase] Failed to update %s -> %s\n", path.c_str(), fbWrite.errorReason().c_str());
    }
    else
    {
        Serial.printf("[Firebase] Updated %s\n", path.c_str());
    }
}

void FirebaseHandler::sendRelayStateSchedule(uint8_t ch, int tempValue, bool scheduleActive, const String &time, bool repeat)
{
    String basePath = "/Home/" + config.device_id + "/switches/sw" + String(ch);

    if (scheduleActive)
    {
        // Schedule ON → update everything
        FirebaseJson json;
        json.set("value", constrain(tempValue, 0, 100));
        json.set("schedule", 1);
        json.set("time", time);
        json.set("repeat", repeat ? 1 : 0);

        if (!Firebase.RTDB.updateNode(&fbWrite, basePath.c_str(), &json))
        {
            Serial.printf("[FirebaseHandler] Failed to update schedule for CH%d -> %s\n",
                          ch, fbWrite.errorReason().c_str());
        }
        else
        {
            Serial.printf("[FirebaseHandler] Schedule updated -> CH%d, val=%d, time=%s, repeat=%d\n",
                          ch, tempValue, time.c_str(), repeat);
        }
    }
    else
    {
        // Schedule OFF → clear fields
        FirebaseJson json;
        json.set("schedule", 0);
        json.set("time", "");
        json.set("repeat", 0);

        if (!Firebase.RTDB.updateNode(&fbWrite, basePath.c_str(), &json))
        {
            Serial.printf("[FirebaseHandler] Failed to clear schedule for CH%d -> %s\n",
                          ch, fbWrite.errorReason().c_str());
        }
        else
        {
            Serial.printf("[FirebaseHandler] Schedule cleared -> CH%d (val=%d)\n", ch, tempValue);
        }
    }
}

void FirebaseHandler::deleteSchedule(uint8_t ch, const String &scheduleId)
{
    String path =
        "/Home/" + config.device_id +
        "/switches/sw" + String(ch) +
        "/schedules/" + scheduleId;

    Firebase.RTDB.deleteNode(&fbWrite, path.c_str());
}

// Loop: must read stream to trigger callbacks
void FirebaseHandler::loop()
{
    // if (!Firebase.RTDB.readStream(&fbStream))
    // {
    //     Serial.printf("[Firebase] Stream read error: %s\n", fbStream.errorReason().c_str());
    // }
    // else
    // {
    //     Firebase.RTDB.setStreamCallback(&fbStream, streamCallback, streamTimeoutCallback);
    //     Serial.println("[Firebase] Stream reconnected.");
    // }
}

void FirebaseHandler::stop()
{
    // Reset Firebase signer so it won’t try to reconnect
    fbConfig.signer.tokens.status = token_status_uninitialized;

    Serial.println("[FIREBASE] Stopped (tokens invalidated)");
}

bool FirebaseHandler::isReady()
{
    return Firebase.ready();
}
