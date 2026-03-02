#include "switchControl.h"
#include "mqttHandler.h"
#include "firebaseHandler.h"
#include "systemState.h"

SwitchControl switchControl;

static constexpr bool ACTIVE_LOW = false; // most relay boards
static portMUX_TYPE triacMux = portMUX_INITIALIZER_UNLOCKED;

#define TRIAC_ON (ACTIVE_LOW ? LOW : HIGH)
#define TRIAC_OFF (ACTIVE_LOW ? HIGH : LOW)

// Timers for publish throttling
static unsigned long lastMqttPublish[MAX_CHANNELS] = {0};
static unsigned long lastFirebasePublish[MAX_CHANNELS] = {0};
static uint8_t lastBrightness[MAX_CHANNELS] = {0};               // remembers last brightness
static constexpr unsigned long mqttPublishIntervalMs = 250;      // fast
static constexpr unsigned long firebasePublishIntervalMs = 1000; // slower
bool scheduledOverride[MAX_CHANNELS] = {false};

// If true, when turning off a relay, the brightness is remembered and restored when turning back on.
// If false, turning off a relay sets brightness to 0, turning on sets brightness to
// static constexpr bool lastUpdateState = false;

// // triac scheduling state (for AC dimming) - shared with ISR & loop
// volatile bool zeroCrossDetected = false;
// volatile unsigned long lastZeroCrossTime = 0;
// volatile unsigned long triacFireAtUs[MAX_CHANNELS];     // timestamp (micros) when we should fire triac
// volatile bool triacPulseActive[MAX_CHANNELS];           // pulse currently active
// volatile unsigned long triacPulseEndAtUs[MAX_CHANNELS]; // when to end pulse

volatile bool zeroCrossDetected = false;
volatile int64_t lastZeroCrossTime = 0;
int64_t nowUs = esp_timer_get_time();
int64_t zcTs;
volatile int64_t triacFireAtUs[MAX_CHANNELS];
volatile bool triacPulseActive[MAX_CHANNELS];
volatile int64_t triacPulseEndAtUs[MAX_CHANNELS];

#define AC_CYCLE_US 10000  // ~10ms per half-cycle (50Hz)
#define MIN_DELAY_US 200   // Safety clamp
#define TRIAC_PULSE_US 100 // length of trigger pulse (tune per hardware)

// keep original arrays but ensure zero-inited
bool cloudPending[MAX_CHANNELS] = {false};
bool cloudState[MAX_CHANNELS] = {false};
bool relayState[MAX_CHANNELS] = {false};
bool switchInitialized[MAX_CHANNELS] = {false};
bool lastUpdateState[MAX_CHANNELS] = {false};
uint8_t relayBrightness[MAX_CHANNELS] = {0};

// For debouncing physical switches (preserved from your code)
int lastSwitchReading[MAX_CHANNELS] = {0};
int switchState[MAX_CHANNELS] = {0};
unsigned long lastDebounceTime[MAX_CHANNELS] = {0};
const unsigned long debounceDelay = 50;

// PWM mapping: map channel idx -> ledc PWM channel
int pwmChannelForIdx[MAX_CHANNELS];

// ----------------- Zero Cross ISR -----------------
// void IRAM_ATTR onZeroCross()
// {
//     uint32_t now = (uint32_t)esp_timer_get_time();
//     if (now - lastZeroCrossTime > 5000)
//     { // ignore spurious triggers <5ms apart
//         zeroCrossDetected = true;
//         lastZeroCrossTime = now;
//     }
// }
void IRAM_ATTR onZeroCross()
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&triacMux);
    if ((now - lastZeroCrossTime) > 5000)
    {
        zeroCrossDetected = true;
        lastZeroCrossTime = now;
    }
    portEXIT_CRITICAL_ISR(&triacMux);
}

// ----------------- Helper: publish state -----------------
void SwitchControl::publishState(uint8_t ch)
{
    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    unsigned long now = millis();
    int idx = ch - 1;

    if (!systemState.servicesStarted)
    {
        cloudState[idx] = relayState[idx];
        cloudPending[idx] = true;
        return;
    }

    // MQTT
    if (systemState.mqttReady && now - lastMqttPublish[idx] > mqttPublishIntervalMs)
    {
        int mqttValue = relayState[idx] ? relayBrightness[idx] : 0; // 0 if OFF, brightness (1–100) if ON
        Serial.printf("[publishState] CH%d -> MQTT %d%%\n", ch, mqttValue);
        mqttHandler.publishRelayState(ch, mqttValue); // updated to send brightness
        lastMqttPublish[idx] = now;                   // update timestamp after publish
    }

    // Firebase
    if (systemState.firebaseReady && now - lastFirebasePublish[idx] > firebasePublishIntervalMs)
    {
        int fbValue = relayState[idx] ? relayBrightness[idx] : 0; // 0 if OFF, brightness (1–100) if ON

        firebaseHandler.sendRelayState(ch, fbValue); // updated to send brightness
        lastFirebasePublish[idx] = now;              // update timestamp after publish
    }

    yield();
}

// ----------------- Initialization -----------------
void SwitchControl::begin()
{
    if (config.zeroCrossPin >= 0)
    {
        pinMode(config.zeroCrossPin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(config.zeroCrossPin), onZeroCross, FALLING);
        Serial.printf("[ZeroCross] Enabled on pin %d\n", config.zeroCrossPin);
    }

    // Allocate PWM channels — use up to 8 ledc channels (0..7). Map 1:1 for simplicity.
    for (int i = 0; i < MAX_CHANNELS; ++i)
        pwmChannelForIdx[i] = i % 8;

    // Relay + Brightness setup
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        int pin = config.relay_pins[i];
        if (pin >= 0)
        {
            pinMode(pin, OUTPUT);
            relayState[i] = config.output_state[i];
            relayBrightness[i] = relayState[i] ? (lastBrightness[i] ? lastBrightness[i] : 100) : 0;
            lastUpdateState[i] = relayState[i];

            // Setup PWM for brightness (DC)
            if (config.switch_type[i] == "dc")
            {
                int pwmChannel = pwmChannelForIdx[i];
                ledcSetup(pwmChannel, 5000, 8);
                ledcAttachPin(pin, pwmChannel);

                uint8_t pwmVal = map(relayBrightness[i], 0, 100, 0, 255);
                if (ACTIVE_LOW)
                    pwmVal = 255 - pwmVal;
                ledcWrite(pwmChannel, pwmVal);
            }
            else // AC
            {
                // if (relayState[i])
                // {
                //     digitalWrite(pin, TRIAC_OFF); // arm triac
                // }
                // else
                // {
                //     digitalWrite(pin, TRIAC_OFF); // keep off
                // }
                digitalWrite(pin, TRIAC_OFF);
            }

            // Serial.printf(
            //     "[RelayMap] CH%d -> pin %d (initial %s)\n",
            //     i + 1,
            //     pin,
            //     relayState[i] ? "ON" : "OFF");
        }
    }

    // Switch setup + mapping
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        int swPin = config.switch_pins[i];
        if (swPin >= 0)
        {
            pinMode(swPin, INPUT_PULLUP); // active LOW
            int r = digitalRead(swPin);
            lastSwitchReading[i] = r;
            switchState[i] = r;

            lastDebounceTime[i] = millis();
            switchInitialized[i] = true;
            // Default SWi → CHi if invalid
            if (config.switch_to_output[i] < 0 || config.switch_to_output[i] >= MAX_CHANNELS)
                config.switch_to_output[i] = i;
            // Serial.printf("[SwitchMap] SW%d -> pin %d → controls Relay CH%d\n",
            //               i + 1, swPin, config.switch_to_output[i] + 1);
        }
    }

    // Fix duplicate mappings
    // for (int i = 0; i < MAX_CHANNELS; i++)
    // {
    //     int mappedRelay = config.switch_to_output[i];
    //     for (int j = 0; j < i; j++)
    //     {
    //         if (config.switch_to_output[j] == mappedRelay)
    //         {
    //             config.switch_to_output[i] = i;
    //             Serial.printf("[FixMap] SW%d had duplicate relay → reassigned to CH%d\n",
    //                           i + 1, i + 1);
    //             break;
    //         }
    //     }
    // }

    // initialize triac arrays
    for (int i = 0; i < MAX_CHANNELS; ++i)
    {
        triacFireAtUs[i] = 0;
        triacPulseActive[i] = false;
        triacPulseEndAtUs[i] = 0;
    }

    Serial.println("SwitchControl initialized (non-blocking AC/DC dimming)");
}

// ----------------- Switch Mapping Swap -----------------
// void SwitchControl::setSwitchMapping(uint8_t sw, uint8_t newChannel)
// {
//     if (sw >= MAX_CHANNELS || newChannel >= MAX_CHANNELS)
//         return;

//     int oldChannel = config.switch_to_output[sw];

//     if (oldChannel == newChannel)
//         return;

//     // Find which switch already uses newChannel
//     for (int i = 0; i < MAX_CHANNELS; i++)
//     {
//         if (i == sw)
//             continue;

//         if (config.switch_to_output[i] == newChannel)
//         {
//             config.switch_to_output[i] = oldChannel; // swap
//             break;
//         }
//     }

//     config.switch_to_output[sw] = newChannel;

//     configManager.save();

//     Serial.printf("[SwapMap] SW%d ↔ CH%d (old CH%d swapped)\n",
//                   sw + 1,
//                   newChannel + 1,
//                   oldChannel + 1);
// }

// ----------------- Relay ON/OFF -----------------
void SwitchControl::setRelay(uint8_t ch, bool state, bool publish)
{
    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    int idx = ch - 1;
    int pin = config.relay_pins[idx];
    if (pin < 0)
        return;

    // Skip if no state change
    if (relayState[idx] == state)
        return;

    // Update internal state
    relayState[idx] = state;
    if (!state)
    {
        lastBrightness[idx] = relayBrightness[idx];
        relayBrightness[idx] = 0;
    }
    else
    {
        if (relayBrightness[idx] == 0)
            relayBrightness[idx] = lastBrightness[idx] ? lastBrightness[idx] : 100;
    }

    // ------------------ Update hardware ------------------
    if (config.switch_type[idx] == "dc")
    {
        // DC dimming uses PWM
        if (!lastUpdateState[idx])
        {
            uint8_t duty = ACTIVE_LOW ? (state ? 0 : 255) : (state ? 255 : 0);
            ledcWrite(pwmChannelForIdx[idx], duty);
        }
        else
        {
            // Optional: remember brightness if turned off
            if (!state)
            {
                lastBrightness[idx] = relayBrightness[idx];
                relayBrightness[idx] = 0;
                ledcWrite(pwmChannelForIdx[idx], ACTIVE_LOW ? 255 : 0);
            }
            else
            {
                if (relayBrightness[idx] == 0)
                    relayBrightness[idx] = lastBrightness[idx];

                uint8_t pwmVal = map(relayBrightness[idx], 0, 100, 0, 255);
                if (ACTIVE_LOW)
                    pwmVal = 255 - pwmVal;
                ledcWrite(pwmChannelForIdx[idx], pwmVal);
            }
        }
    }
    else if (config.switch_type[idx] == "ac")
    {
        // AC channel → we set brightness for scheduling (triac handled by loop())
        if (state)
        {
            relayBrightness[idx] = 100;
            digitalWrite(pin, TRIAC_ON); // arm optotriac gate
        }

        else
        {
            relayBrightness[idx] = 0;
            // Cancel any pending fire
            noInterrupts();
            triacFireAtUs[idx] = 0;
            triacPulseActive[idx] = false;
            triacPulseEndAtUs[idx] = 0;
            interrupts();
            digitalWrite(pin, TRIAC_OFF); // Ensure triac off
        }
    }

    // ------------------------------------------------------

    if (publish)
        publishState(ch);

    Serial.printf("[setRelay] CH%d -> pin %d → %s (type=%s, brightness=%d)\n",
                  ch, pin, state ? "ON" : "OFF",
                  config.switch_type[idx].c_str(), relayBrightness[idx]);

    lastUpdateState[idx] = state;
    config.output_state[idx] = state;
    static unsigned long lastSave = 0;
    if (millis() - lastSave > 2000)
    {
        configManager.save();
        lastSave = millis();
    }
}

// ----------------- Brightness -----------------
void SwitchControl::setBrightness(uint8_t ch, uint8_t value, bool publish)
{
    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    int idx = ch - 1;
    int pin = config.relay_pins[idx];
    if (pin < 0)
        return;

    value = constrain(value, 0, 100);

    relayBrightness[idx] = value;
    relayState[idx] = (value > 0);

    // Hardware
    if (config.switch_type[idx] == "dc")
    {
        uint8_t pwmVal = map(value, 0, 100, 0, 255);
        if (ACTIVE_LOW)
            pwmVal = 255 - pwmVal;
        ledcWrite(pwmChannelForIdx[idx], pwmVal);
    }
    else
    {
        digitalWrite(pin, TRIAC_OFF);
    }

    if (publish)
        publishState(ch);

    Serial.printf("[setBrightness] CH%d -> %d%% (%s)\n",
                  ch, value, config.switch_type[idx].c_str());

    //  keep state consistent
    lastUpdateState[idx] = relayState[idx];
    config.output_state[idx] = relayState[idx];

    static unsigned long lastSave = 0;
    if (millis() - lastSave > 2000)
    {
        configManager.save();
        lastSave = millis();
    }
}

// ----------------- Helpers -----------------
bool SwitchControl::getState(uint8_t ch)
{
    if (ch < 1 || ch > MAX_CHANNELS)
        return false;
    return relayState[ch - 1];
}

uint8_t SwitchControl::getBrightness(uint8_t ch)
{
    if (ch < 1 || ch > MAX_CHANNELS)
        return 0;
    return relayBrightness[ch - 1];
}

void SwitchControl::toggleRelay(uint8_t ch, bool publish)
{
    if (ch < 1 || ch > MAX_CHANNELS)
        return;
    setRelay(ch, !relayState[ch - 1], publish);
}

void SwitchControl::enqueueCloudSet(uint8_t ch, bool state)
{
    if (ch < 1 || ch > MAX_CHANNELS)
        return;

    int idx = ch - 1;
    cloudState[idx] = state;
    cloudPending[idx] = true;
}

// ----------------- Loop -----------------
static unsigned long lastCloudApply = 0;
const unsigned long cloudApplyInterval = 150;

void SwitchControl::loop()
{
    unsigned long nowMs = millis();
    unsigned long nowUs = micros();

    // --- AC Dimming Handler (schedule triac firings on zero crossing) ---
    if (zeroCrossDetected)
    {
        // Capture and clear flag quickly (protect shared volatile)
        noInterrupts();
        unsigned long zcTs = lastZeroCrossTime;
        zeroCrossDetected = false;
        interrupts();

        for (int i = 0; i < MAX_CHANNELS; i++)
        {
            if (config.switch_type[i] != "ac" || config.relay_pins[i] < 0)
                continue;

            uint8_t b = relayBrightness[i]; // 0..100
            if (b == 0)
            {
                // cancel any pending fire
                noInterrupts();
                triacFireAtUs[i] = 0;
                triacPulseActive[i] = false;
                triacPulseEndAtUs[i] = 0;
                interrupts();
                continue;
            }

            // Map brightness to delay (us): 0->late (near end), 100->early (near zero-cross)
            int delayUs = map(b, 0, 100, 8000, 200);
            delayUs = constrain(delayUs, MIN_DELAY_US, AC_CYCLE_US - 100);

            unsigned long fireAt = zcTs + (unsigned long)delayUs;

            // store scheduled fire time (atomic-ish)
            noInterrupts();
            triacFireAtUs[i] = fireAt;
            // ensure we are not currently pulsing
            triacPulseActive[i] = false;
            triacPulseEndAtUs[i] = 0;
            interrupts();
        }
    }

    // --- Execute triac pulses when scheduled (non-blocking) ---
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        if (config.switch_type[i] != "ac" || config.relay_pins[i] < 0)
            continue;

        // Read scheduled time and active flags atomically-ish
        noInterrupts();
        int64_t scheduledAt = triacFireAtUs[i];
        bool active = triacPulseActive[i];
        int64_t pulseEnd = triacPulseEndAtUs[i];
        interrupts();

        // If it's time to start pulse
        if (scheduledAt != 0 && (long)(nowUs - scheduledAt) >= 0 && !active)
        {
            // start pulse
            // digitalWrite(config.relay_pins[i], ACTIVE_LOW ? HIGH : HIGH); // pulse active level (depends on optotriac)
            digitalWrite(config.relay_pins[i], TRIAC_ON);
            noInterrupts();
            triacPulseActive[i] = true;
            triacPulseEndAtUs[i] = nowUs + TRIAC_PULSE_US;
            // clear scheduledAt so we only fire once per zero-cross
            triacFireAtUs[i] = 0;
            interrupts();
        }

        // If pulse active and end time reached -> stop pulse
        if (active && (long)(nowUs - pulseEnd) >= 0)
        {
            // digitalWrite(config.relay_pins[i], ACTIVE_LOW ? LOW : LOW); // back to idle
            digitalWrite(config.relay_pins[i], TRIAC_OFF);
            noInterrupts();
            triacPulseActive[i] = false;
            triacPulseEndAtUs[i] = 0;
            interrupts();
        }

        // allow background tasks to run a tiny bit in long loops
        // but avoid calling delay() here to preserve pulse timing — use yield()
        // yield();
    }

    // --- Cloud Sync Updates (apply cloud pending states throttled) ---
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        if (cloudPending[i] && nowMs - lastCloudApply >= cloudApplyInterval)
        {
            cloudPending[i] = false;
            publishState(i + 1);
            lastCloudApply = nowMs;
            break; // do one per loop to avoid long blocking
        }
    }

    // --- Physical Switch Handling (FULLY FIXED) ---
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        if (config.switch_pins[i] < 0)
            continue;

        bool reading = digitalRead(config.switch_pins[i]);

        // -------- Debounce --------
        if (reading != lastSwitchReading[i])
            lastDebounceTime[i] = millis();

        if ((millis() - lastDebounceTime[i]) < debounceDelay)
        {
            lastSwitchReading[i] = reading;
            continue;
        }

        // -------- Stable state change --------
        if (reading != switchState[i])
        {
            bool previousState = switchState[i];
            switchState[i] = reading;

            int relayIndex = config.switch_to_output[i];
            if (relayIndex < 0 || relayIndex >= MAX_CHANNELS)
                continue;

            String logic = config.switch_logic[i];

            Serial.printf(
                "[Switch] SW%d %s -> %s (logic=%s) → CH%d\n",
                i + 1,
                previousState == HIGH ? "HIGH" : "LOW",
                reading == HIGH ? "HIGH" : "LOW",
                logic.c_str(),
                relayIndex + 1);

            // ================= LOGIC HANDLING =================

            // -------- TOGGLE (Latching switch) --------
            if (logic == "toggle")
            {
                if (switchState[i] == LOW)
                    toggleRelay(relayIndex + 1, false);
            }
            // -------- BELL (Momentary push) --------
            if (logic == "bell")
            {
                static unsigned long bellOffAt[MAX_CHANNELS] = {0};

                if (previousState == HIGH && reading == LOW) // press
                {
                    setRelay(relayIndex + 1, true, false);
                    bellOffAt[relayIndex] = millis() + 500; // 500ms pulse
                }

                // auto-off
                for (int j = 0; j < MAX_CHANNELS; j++)
                {
                    if (bellOffAt[j] && millis() > bellOffAt[j])
                    {
                        setRelay(j + 1, false, false);
                        bellOffAt[j] = 0;
                    }
                }
            }

            // -------- PUSH (Follow switch state) --------
            if (logic == "push")
            {
                bool pressed = (switchState[i] == LOW);

                // RELEASED -> PRESSED  => ON
                if (previousState == HIGH && pressed)
                {
                    setRelay(relayIndex + 1, true, true);

                    Serial.printf(
                        "[Push] SW%d RELEASED→PRESSED | Relay CH%d ON\n",
                        i + 1,
                        relayIndex + 1);
                }
                // PRESSED -> RELEASED  => OFF
                else if (previousState == LOW && !pressed)
                {
                    setRelay(relayIndex + 1, false, true);

                    Serial.printf(
                        "[Push] SW%d PRESSED→RELEASED | Relay CH%d OFF\n",
                        i + 1,
                        relayIndex + 1);
                }
            }

            // -------- 2WAY (Latching staircase) --------
            else if (logic == "2way")
            {
                // Same as toggle: ANY state change
                toggleRelay(relayIndex + 1, false);

                Serial.printf(
                    "[2Way] SW%d state change → CH%d toggled\n",
                    i + 1, relayIndex + 1);
            }

            cloudPending[relayIndex] = true;
        }

        lastSwitchReading[i] = reading;
    }

    // Allow background tasks
    yield();

    static bool bootCloudSyncDone = false;

    if (systemState.servicesStarted && !bootCloudSyncDone)
    {
        for (int i = 0; i < MAX_CHANNELS; i++)
        {
            publishState(i + 1);
        }

        bootCloudSyncDone = true;
        Serial.println("[BOOT] Cloud synced from saved relay state");
    }
}
