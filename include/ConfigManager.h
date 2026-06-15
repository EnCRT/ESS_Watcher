#pragma once

#include <Preferences.h>
#include "secrets.h"

#define DEFAULT_MSG_STARTUP_POWER   "🚀 ⚡️ ✅ ESS Watcher запущен! Сеть 220В есть."
#define DEFAULT_MSG_STARTUP_BATTERY "⚡️ ❌ ⚠️ ESS Watcher запущен! Питание от АКБ. ❌"
#define DEFAULT_MSG_POWER_RESTORED  "⚡♻️ Сеть 220В ВОССТАНОВЛЕНА!"
#define DEFAULT_MSG_POWER_LOST      "🚨 ОТКЛЮЧЕНИЕ ПИТАНИЯ 220В!"

class ConfigManager {
private:
    Preferences preferences;
    
    struct Config {
        char wifi_ssid[64];
        char wifi_pass[64];
        char bot_token[128];
        char chat_id[64];
        char thread_id[32];
        char melody_on_file[32];
        char melody_off_file[32];
        uint8_t buzzer_volume;
        char msg_startup_power[200];
        char msg_startup_battery[200];
        char msg_power_restored[200];
        char msg_power_lost[200];
    } data;

public:
    ConfigManager() {}

    void begin() {
        preferences.begin("ess-watcher", false);
        load();
    }

    void load() {
        // Загружаем данные, если их нет - используем дефолты из secrets.h
        size_t read = preferences.getBytes("config", &data, sizeof(Config));
        if (read != sizeof(Config)) {
            Serial.println("ℹ️ Config: No stored settings found. Loading defaults from secrets.h.");
            
            strncpy(data.wifi_ssid, SECRET_WIFI_SSID, sizeof(data.wifi_ssid));
            strncpy(data.wifi_pass, SECRET_WIFI_PASSWORD, sizeof(data.wifi_pass));
            strncpy(data.bot_token, SECRET_BOT_TOKEN, sizeof(data.bot_token));
            strncpy(data.chat_id, SECRET_CHAT_ID, sizeof(data.chat_id));
            strncpy(data.thread_id, SECRET_MESSAGE_THREAD_ID, sizeof(data.thread_id));
            data.melody_on_file[0] = '\0';
            data.melody_off_file[0] = '\0';
            data.buzzer_volume = 100;
            strncpy(data.msg_startup_power, DEFAULT_MSG_STARTUP_POWER, sizeof(data.msg_startup_power));
            strncpy(data.msg_startup_battery, DEFAULT_MSG_STARTUP_BATTERY, sizeof(data.msg_startup_battery));
            strncpy(data.msg_power_restored, DEFAULT_MSG_POWER_RESTORED, sizeof(data.msg_power_restored));
            strncpy(data.msg_power_lost, DEFAULT_MSG_POWER_LOST, sizeof(data.msg_power_lost));
            
            save(); // Сохраняем дефолты
        } else {
            Serial.println("✅ Config: Settings loaded from NVS.");
        }
    }

    void save() {
        preferences.putBytes("config", &data, sizeof(Config));
        Serial.println("💾 Config: Settings saved to NVS.");
    }

    // Getters
    const char* getSSID() const { return data.wifi_ssid; }
    const char* getPass() const { return data.wifi_pass; }
    const char* getBotToken() const { return data.bot_token; }
    const char* getChatID() const { return data.chat_id; }
    const char* getThreadID() const { return data.thread_id; }
    const char* getMelodyOnFile() const { return data.melody_on_file; }
    const char* getMelodyOffFile() const { return data.melody_off_file; }
    uint8_t getBuzzerVolume() const { return data.buzzer_volume; }
    const char* getMsgStartupPower() const { return data.msg_startup_power; }
    const char* getMsgStartupBattery() const { return data.msg_startup_battery; }
    const char* getMsgPowerRestored() const { return data.msg_power_restored; }
    const char* getMsgPowerLost() const { return data.msg_power_lost; }

    // Setters (with safety)
    void setSSID(const String& val) { strncpy(data.wifi_ssid, val.c_str(), sizeof(data.wifi_ssid)); }
    void setPass(const String& val) { strncpy(data.wifi_pass, val.c_str(), sizeof(data.wifi_pass)); }
    void setBotToken(const String& val) { strncpy(data.bot_token, val.c_str(), sizeof(data.bot_token)); }
    void setChatID(const String& val) { strncpy(data.chat_id, val.c_str(), sizeof(data.chat_id)); }
    void setThreadID(const String& val) { strncpy(data.thread_id, val.c_str(), sizeof(data.thread_id)); }
    void setMelodyOnFile(const String& val) { strncpy(data.melody_on_file, val.c_str(), sizeof(data.melody_on_file)); }
    void setMelodyOffFile(const String& val) { strncpy(data.melody_off_file, val.c_str(), sizeof(data.melody_off_file)); }
    void setBuzzerVolume(uint8_t vol) { data.buzzer_volume = constrain(vol, (uint8_t)0, (uint8_t)100); }
    void setMsgStartupPower(const String& val) { strncpy(data.msg_startup_power, val.c_str(), sizeof(data.msg_startup_power)); }
    void setMsgStartupBattery(const String& val) { strncpy(data.msg_startup_battery, val.c_str(), sizeof(data.msg_startup_battery)); }
    void setMsgPowerRestored(const String& val) { strncpy(data.msg_power_restored, val.c_str(), sizeof(data.msg_power_restored)); }
    void setMsgPowerLost(const String& val) { strncpy(data.msg_power_lost, val.c_str(), sizeof(data.msg_power_lost)); }
};
