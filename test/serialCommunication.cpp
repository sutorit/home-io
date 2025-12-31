#include "serialCommunication.h"
#include "switchControl.h"
#include "systemState.h"

extern SwitchControl switchControl;

SerialCommunication serialCom;

// ------------------- INIT -------------------
void SerialCommunication::begin(long baudRate, int rxPin, int txPin)
{
    Serial.begin(baudRate);
    Serial2.begin(baudRate, SERIAL_8N1, rxPin, txPin);
    Serial.println("[SerialCom] USB + UART2 ready");
    uartBuffer.reserve(128);
    usbBuffer.reserve(128);
}

// ------------------- LOOP -------------------
void SerialCommunication::loop()
{
    // Handle USB Serial
    while (Serial.available())
    {
        char c = Serial.read();
        if (c == '\n' || c == '\r')
        {
            if (usbBuffer.length() > 0)
            {
                processIncomingCommand(usbBuffer, Serial);
                usbBuffer = "";
            }
        }
        else
            usbBuffer += c;
    }

    // Handle UART2
    while (Serial2.available())
    {
        char c = Serial2.read();
        if (c == '\n' || c == '\r')
        {
            if (uartBuffer.length() > 0)
            {
                processIncomingCommand(uartBuffer, Serial2);
                uartBuffer = "";
            }
        }
        else
            uartBuffer += c;
    }
}

// ------------------- SEND HELPERS -------------------
void SerialCommunication::sendMessage(const String &msg)
{
    Serial.println(msg);
    Serial2.println(msg);
}

// ------------------- NOTIFICATIONS -------------------
void SerialCommunication::notifyRelayChange(uint8_t ch, bool state, uint8_t brightness)
{
    String msg = "[UPDATE] CH" + String(ch) + "=" + (state ? "ON" : "OFF") +
                 " BRIGHT=" + String(brightness);
    sendMessage(msg);
}

void SerialCommunication::notifyBrightnessChange(uint8_t ch, uint8_t value)
{
    String msg = "[BRIGHTNESS] CH" + String(ch) + "=" + String(value);
    sendMessage(msg);
}

// ------------------- COMMAND HANDLER -------------------
void SerialCommunication::processIncomingCommand(const String &cmd, HardwareSerial &source)
{
    String command = cmd;
    command.trim();
    command.toUpperCase();

    Serial.printf("[Serial RX] %s\n", command.c_str());
    handleCommand(command);
}

static int parseNextInt(const String &s, int startIdx, int &outIdx)
{
    // skip spaces
    int i = startIdx;
    while (i < (int)s.length() && isspace(s[i]))
        ++i;
    int val = 0;
    bool found = false;
    while (i < (int)s.length() && isdigit(s[i]))
    {
        found = true;
        val = val * 10 + (s[i] - '0');
        ++i;
    }
    outIdx = i;
    return found ? val : -1;
}

void SerialCommunication::handleCommand(const String &rawCmd)
{
    // make a working uppercase trimmed copy
    String cmd = rawCmd;
    cmd.trim();

    // normalize spaces: replace multiple spaces with single space
    String norm;
    bool lastSpace = false;
    for (size_t i = 0; i < cmd.length(); ++i)
    {
        char c = cmd[i];
        if (isspace(c))
        {
            if (!lastSpace)
            {
                norm += ' ';
                lastSpace = true;
            }
        }
        else
        {
            norm += toupper(c);
            lastSpace = false;
        }
    }
    cmd = norm;

    if (cmd.length() == 0)
    {
        sendMessage("ERR EMPTY");
        return;
    }

    // Tokenize by spaces
    // We'll support patterns:
    // CH <n> ON
    // CH <n> OFF
    // BRIGHT <n> <v>
    // GET CH <n>
    // GET BRIGHT <n>
    // STATUS
    if (cmd == "STATUS")
    {
        for (int i = 1; i <= MAX_CHANNELS; ++i)
        {
            bool st = switchControl.getState(i);
            uint8_t br = switchControl.getBrightness(i);
            sendMessage("CH" + String(i) + "=" + (st ? "ON" : "OFF") + " BRIGHT=" + String(br));
        }
        sendMessage("END");
        return;
    }

    // startswith "CH " ?
    if (cmd.startsWith("CH "))
    {
        int idx = 3; // position after "CH "
        int nextIdx;
        int ch = parseNextInt(cmd, idx, nextIdx);
        if (ch < 1 || ch > MAX_CHANNELS)
        {
            sendMessage("ERR CHANNEL");
            return;
        }
        // check for ON/OFF after number
        String tail = cmd.substring(nextIdx);
        tail.trim();
        if (tail == "ON")
        {
            switchControl.setRelay(ch, true, true);
            sendMessage("OK CH" + String(ch) + " ON");
            // notifyRelayChange will broadcast update
            return;
        }
        else if (tail == "OFF")
        {
            switchControl.setRelay(ch, false, true);
            sendMessage("OK CH" + String(ch) + " OFF");
            return;
        }
        else
        {
            sendMessage("ERR SYNTAX");
            return;
        }
    }

    // startswith "BRIGHT "
    if (cmd.startsWith("BRIGHT "))
    {
        int idx = 7;
        int afterCh;
        int ch = parseNextInt(cmd, idx, afterCh);
        if (ch < 1 || ch > MAX_CHANNELS)
        {
            sendMessage("ERR CHANNEL");
            return;
        }
        int afterVal;
        int val = parseNextInt(cmd, afterCh, afterVal);
        if (val < 0 || val > 100)
        {
            sendMessage("ERR VALUE (0..100)");
            return;
        }
        switchControl.setBrightness(ch, (uint8_t)val, true);
        sendMessage("OK BRIGHT CH" + String(ch) + "=" + String(val));
        return;
    }

    // startswith "GET "
    if (cmd.startsWith("GET "))
    {
        String tail = cmd.substring(4);
        tail.trim();
        if (tail.startsWith("CH "))
        {
            int idx = 3;
            int nextIdx;
            int ch = parseNextInt(tail, idx, nextIdx);
            if (ch < 1 || ch > MAX_CHANNELS)
            {
                sendMessage("ERR CHANNEL");
                return;
            }
            bool st = switchControl.getState(ch);
            uint8_t br = switchControl.getBrightness(ch);
            sendMessage("CH" + String(ch) + "=" + (st ? "ON" : "OFF") + " BRIGHT=" + String(br));
            return;
        }
        else if (tail.startsWith("BRIGHT "))
        {
            int idx = 7;
            int nextIdx;
            int ch = parseNextInt(tail, idx, nextIdx);
            if (ch < 1 || ch > MAX_CHANNELS)
            {
                sendMessage("ERR CHANNEL");
                return;
            }
            uint8_t br = switchControl.getBrightness(ch);
            sendMessage("CH" + String(ch) + " BRIGHT=" + String(br));
            return;
        }
        else
        {
            sendMessage("ERR SYNTAX");
            return;
        }
    }

    sendMessage("UNKNOWN CMD");
}
