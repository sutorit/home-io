# SHOCKSO 8CH Home Automation

An **ESP32-based 8-channel home automation system** supporting Wi-Fi, MQTT, Firebase, and Web Configuration Portal.  
The project provides **relay/switch control with dimming**, automatic **Wi-Fi STA/AP switching**, and **self-healing services** when the internet drops or reconnects.  

---

## 🚀 Features
- **Wi-Fi Auto-Management**  
  - Starts in **STA mode**  
  - Falls back to **AP mode** if STA connection fails  
  - Switches back to STA when Wi-Fi reconnects  
- **Web Portal**  
  - Runs in both AP & STA mode  
  - Configuration stored in `/config.json` (SPIFFS)  
- **MQTT Integration** (relay control & monitoring)  
- **Firebase Integration** (cloud sync support)  
- **Relay/Switch Control** with optional dimming  
- **Automatic Internet Recovery** (restarts MQTT/Firebase when Wi-Fi reconnects)  
- **Unique Device ID** generated from ESP32 MAC address  

---

## 📂 Project Structure
```
/src
 ├── main.cpp              → Main entry, Wi-Fi + service management
 ├── wifiManager.cpp/h     → STA/AP auto switching
 ├── configManager.cpp/h   → Load/save configuration from SPIFFS
 ├── webServer.cpp/h       → Web portal for config & control
 ├── switchControl.cpp/h   → Relay & switch handling
 ├── mqttHandler.cpp/h     → MQTT connection & loop
 ├── firebaseHandler.cpp/h → Firebase connection & loop
 ├── systemState.h         → Runtime flags (services ready, etc.)
 └── Utils.h               → Internet check, reboot helpers
```

---

## ⚡ Wi-Fi Behavior
1. On boot, ESP32 tries to load configuration from `/config.json`  
2. If no config → starts in **AP mode** (`SHOCKSO_<MAC>`, default pass `12345678`)  
3. If config exists → tries to connect in **STA mode**  
4. If STA fails → fallback to **AP mode**  
5. If STA reconnects later → switches back from AP → STA  

---

## 🛠️ Configuration
Stored in `/config.json` (SPIFFS). Example:
```json
{
  "device_name": `SHOCKSO_<MAC>`,
  "device_password": "password",
  "wifi_ssid": "MyWiFi",
  "wifi_password": "MyPassword",
  "mqtt_broker": "192.168.1.10",
  "mqtt_port": 1883,
  "mqtt_user": "user",
  "mqtt_pass": "pass",
  "firebase_url": "https://myproject.firebaseio.com",
  "firebase_key": "API_KEY",
  "mode": "master",
  "wifi_ip": "192.168.XX.XX",
  "wifi_ipgetway": "192.168.1.1",
  "wifi_subnetmask": "255.255.255.0",
  "wifi_dns": "8.8.8.8"
}
```

---

## 🔗 MQTT Topics
Example (customizable in code):  
- `shockso/<device_id>/relay/<ch>/set` → Control relay state  
- `shockso/<device_id>/relay/<ch>/state` → Publish current state  

---

## 🌐 Web Portal
- Accessible in both AP & STA mode  
- Default AP: `192.168.4.1`  
- Configure Wi-Fi, MQTT, Firebase, and relay mapping  

---

## 🔄 Service Recovery
- Internet checked every **10 seconds**  
- If connection lost → stops MQTT/Firebase services  
- If connection restored → services automatically restart  

---

## 📖 Flow Diagram
```
ESP32 Boot
    │
    ▼
WiFi Config?
   ├── No → AP Mode (SHOCKSO_<MAC>)
   └── Yes → STA Mode (WiFi + Services)
```

---

## 📦 Requirements
- **ESP32 DevKit** (with at least 8 GPIOs free for relays)  
- **Arduino IDE / PlatformIO**  
- Libraries:  
  - `WiFi.h` (built-in)  
  - `ArduinoJson`  
  - `SPIFFS`  
  - `PubSubClient` (MQTT)  
  - Firebase library  

---

## ⚙️ Installation
1. Clone repository  
2. Flash code to ESP32  
3. First boot → device starts in **AP mode** (`SHOCKSO_<MAC>`)  
4. Connect to AP & open web portal (`192.168.4.1`)  
5. Enter Wi-Fi + MQTT/Firebase settings  
6. Save & reboot → ESP32 connects in STA mode  

---

## 📌 Default Settings
- **AP SSID:** `SHOCKSO_<MAC>`  
- **AP Password:** `12345678`  
- **Device Password (config):** `"password"`  

---

## 🙋 How to Use
1. **First Setup**  
   - Power on device → It starts in **AP mode** (if no config).  
   - Connect phone/PC to Wi-Fi SSID `SHOCKSO_<MAC>`.  
   - Open `192.168.4.1` in browser → Setup page appears.  
   - Enter your home Wi-Fi, MQTT broker, Firebase credentials, and relay mapping.  

2. **Normal Operation**  
   - Device connects to Wi-Fi (STA mode).  
   - Relays can be controlled via **MQTT**, **Firebase**, or **local switches**.  
   - State updates are synced to MQTT/Firebase.  

3. **Offline Mode**  
   - If Wi-Fi disconnects → device automatically switches to **AP mode**.  
   - You can reconnect to `SHOCKSO_<MAC>` and reconfigure.  

4. **Relay Control**  
   - Relays can be toggled via:  
     - **MQTT commands** (mosquitto_pub -h <broker_ip> -u <username> -P <password> -t shockso/<device_id>/relay/<ch>/ -m '{"state":"on"}')  
     - **Firebase updates**  
     - **Physical switches** connected to ESP32 pins  

5. **Recovery**  
   - If internet drops → services pause automatically.  
   - Once Wi-Fi/internet is restored → MQTT + Firebase reconnect automatically.  

---

mqtt set value like
 mosquitto_pub -h localhost -u (username) -P (password) -t shockso/(device_id)/relay/(ch) -m "{\"state\":\"on\"}"


## 📝 License
MIT License. Free to use and modify.  
