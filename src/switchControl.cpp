#include "switchControl.h"
#include "mqttHandler.h"
#include "firebaseHandler.h"
#include "systemState.h"

SwitchControl switchControl;

static constexpr bool ACTIVE_LOW = false; // most relay boards
static portMUX_TYPE triacMux = portMUX_INITIALIZER_UNLOCKED;

// Timers for publish throttling
static unsigned long lastMqttPublish[MAX_CHANNELS] = {0};
static unsigned long lastFirebasePublish[MAX_CHANNELS] = {0};
static uint8_t lastBrightness[MAX_CHANNELS] = {0};               // remembers last brightness
static constexpr unsigned long mqttPublishIntervalMs = 250;      // fast
static constexpr unsigned long firebasePublishIntervalMs = 1000; // slower
bool scheduledOverride[MAX_CHANNELS] = {false};

// If true, when turning off a relay, the brightness is remembered and restored when turning back on.
// If false, turning off a relay sets brightness to 0, turning on sets brightness to
static constexpr bool lastUpdateState = false;

// // triac scheduling state (for AC dimming) - shared with ISR & loop
// volatile bool zeroCrossDetected = false;
// volatile unsigned long lastZeroCrossTime = 0;
// volatile unsigned long triacFireAtUs[MAX_CHANNELS];     // timestamp (micros) when we should fire triac
// volatile bool triacPulseActive[MAX_CHANNELS];           // pulse currently active
// volatile unsigned long triacPulseEndAtUs[MAX_CHANNELS]; // when to end pulse

volatile bool zeroCrossDetected = false;
volatile int64_t lastZeroCrossTime = 0;
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

    // MQTT (fast)
    if (systemState.mqttReady && now - lastMqttPublish[idx] > mqttPublishIntervalMs)
    {
        mqttHandler.publishRelayState(ch, relayState[idx], relayBrightness[idx]);
        lastMqttPublish[idx] = now;
    }

    // Firebase (slower, MUST USE BRIGHTNESS)
    if (systemState.firebaseReady && now - lastFirebasePublish[idx] > firebasePublishIntervalMs)
    {
        int fbValue = relayState[idx] ? relayBrightness[idx] : 0;

        firebaseHandler.sendRelayState(
            ch,
            fbValue
        );

        lastFirebasePublish[idx] = now;
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
            relayState[i] = false;
            relayBrightness[i] = 0;

            // Setup PWM for brightness (DC)
            if (config.switch_type[i] == "dc")
            {
                int pwmChannel = pwmChannelForIdx[i];
                // Use 8-bit resolution
                ledcSetup(pwmChannel, 5000, 8);
                ledcAttachPin(pin, pwmChannel);

                uint8_t pwmOff = ACTIVE_LOW ? 255 : 0;
                ledcWrite(pwmChannel, pwmOff); // use pwmChannel, not i
            }
            else
            {
                digitalWrite(pin, ACTIVE_LOW ? HIGH : LOW); // ensure triac off
            }
            Serial.printf("[RelayMap] CH%d -> pin %d (initial OFF)\n", i + 1, pin);
        }
    }

    // Switch setup + mapping
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        int swPin = config.switch_pins[i];
        if (swPin >= 0)
        {
            pinMode(swPin, INPUT_PULLUP); // active LOW
            lastSwitchReading[i] = HIGH;
            switchState[i] = HIGH;
            lastDebounceTime[i] = millis();
            // Default SWi → CHi if invalid
            if (config.switch_to_output[i] < 0 || config.switch_to_output[i] >= MAX_CHANNELS)
                config.switch_to_output[i] = i;
            Serial.printf("[SwitchMap] SW%d -> pin %d → controls Relay CH%d\n",
                          i + 1, swPin, config.switch_to_output[i] + 1);
        }
    }

    // Fix duplicate mappings
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        int mappedRelay = config.switch_to_output[i];
        for (int j = 0; j < i; j++)
        {
            if (config.switch_to_output[j] == mappedRelay)
            {
                config.switch_to_output[i] = i;
                Serial.printf("[FixMap] SW%d had duplicate relay → reassigned to CH%d\n",
                              i + 1, i + 1);
                break;
            }
        }
    }

    // initialize triac arrays
    for (int i = 0; i < MAX_CHANNELS; ++i)
    {
        triacFireAtUs[i] = 0;
        triacPulseActive[i] = false;
        triacPulseEndAtUs[i] = 0;
    }

    Serial.println("SwitchControl initialized (non-blocking AC/DC dimming)");
}

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
    relayBrightness[idx] = state ? 100 : 0;

    // ------------------ Update hardware ------------------
    if (config.switch_type[idx] == "dc")
    {
        // DC dimming uses PWM
        if (!lastUpdateState)
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
            digitalWrite(pin, ACTIVE_LOW ? HIGH : LOW); // Ensure triac off
        }
    }

    // ------------------------------------------------------

    if (publish)
        publishState(ch);

    Serial.printf("[setRelay] CH%d -> pin %d → %s (type=%s, brightness=%d)\n",
                  ch, pin, state ? "ON" : "OFF",
                  config.switch_type[idx].c_str(), relayBrightness[idx]);
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

    // Update hardware
    if (config.switch_type[idx] == "dc")
    {
        uint8_t pwmVal = map(value, 0, 100, 0, 255);
        if (ACTIVE_LOW)
            pwmVal = 255 - pwmVal;
        ledcWrite(pwmChannelForIdx[idx], pwmVal);
    }
    else
    {
        // For AC dimming, brightness handled in loop() after zero-cross
        // Ensure output idle state is correct (triac idle)
        digitalWrite(pin, ACTIVE_LOW ? HIGH : LOW); // default off
    }

    if (publish)
        publishState(ch);

    Serial.printf("[setBrightness] CH%d -> %d%% (%s)\n", ch + 1 - 1 + 1, value, config.switch_type[idx].c_str());
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
    cloudState[ch - 1] = state;
    cloudPending[ch - 1] = true;
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
        unsigned long scheduledAt = triacFireAtUs[i];
        bool active = triacPulseActive[i];
        unsigned long pulseEnd = triacPulseEndAtUs[i];
        interrupts();

        // If it's time to start pulse
        if (scheduledAt != 0 && (long)(nowUs - scheduledAt) >= 0 && !active)
        {
            // start pulse
            // digitalWrite(config.relay_pins[i], ACTIVE_LOW ? HIGH : HIGH); // pulse active level (depends on optotriac)
            digitalWrite(config.relay_pins[i], HIGH);
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
            digitalWrite(config.relay_pins[i], LOW);
            noInterrupts();
            triacPulseActive[i] = false;
            triacPulseEndAtUs[i] = 0;
            interrupts();
        }

        // allow background tasks to run a tiny bit in long loops
        // but avoid calling delay() here to preserve pulse timing — use yield()
        yield();
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

    // --- Physical Switch Handling (for manual press) ---
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        if (config.switch_pins[i] < 0)
            continue;

        bool reading = digitalRead(config.switch_pins[i]);
        if (reading != lastSwitchReading[i])
            lastDebounceTime[i] = millis();

        if ((millis() - lastDebounceTime[i]) > debounceDelay)
        {
            if (reading != switchState[i])
            {
                switchState[i] = reading;

                int relayIndex = config.switch_to_output[i];
                if (relayIndex < 0 || relayIndex >= MAX_CHANNELS)
                    continue;

                String logic = config.switch_logic[i];
                Serial.printf("[Switch] SW%d changed (logic=%s) → Relay CH%d\n",
                              i + 1, logic.c_str(), relayIndex + 1);

                if (logic == "toggle")
                {
                    if (switchState[i] == LOW)
                        toggleRelay(relayIndex + 1, false);
                }
                else if (logic == "bell")
                {
                    if (switchState[i] == LOW)
                    {
                        setRelay(relayIndex + 1, true, false);
                        delay(200);
                        setRelay(relayIndex + 1, false, false);
                    }
                }
                else if (logic == "push")
                {
                    bool pressed = (switchState[i] == LOW); // active LOW button
                    setRelay(relayIndex + 1, pressed, false);
                    Serial.printf("[Push] SW%d %s -> CH%d = %s\n",
                                  i + 1,
                                  pressed ? "PRESSED" : "RELEASED",
                                  relayIndex + 1,
                                  pressed ? "ON" : "OFF");
                }
                else if (logic == "2way")
                {
                    if (switchState[i] == LOW)
                    {
                        toggleRelay(relayIndex + 1, false);
                        Serial.printf("[2Way] CH%d toggled by SW%d\n", relayIndex + 1, i + 1);
                    }
                }

                cloudPending[relayIndex] = true;
            }
        }
        lastSwitchReading[i] = reading;
    }

    // Very small pause to let async_tcp & WiFi run for a moment.
    // We used yield() in key places; this delay(0) is safe and non-blocking-ish.
    delay(0);
}
