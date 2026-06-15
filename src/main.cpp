#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <time.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "secrets.h"
#include "ConfigManager.h"
#include "WebPortal.h"
#include "BuzzerManager.h"
#include "SoundStorage.h"

// =====================================================================================
// =                               НАСТРОЙКИ ПИНОВ                                     =
// =====================================================================================

const int PIN_POWER_SENSE = 0; // Пин для мониторинга 3.3В (через делитель на 3.3В!) от блока питания 220В
const int PIN_BUZZER = 2;      // Пин для зуммера
const int PIN_LED_DATA = 10;   // Пин для WS2812B
const int PIN_CONFIG_BUTTON = 9; // Пин для кнопки настройки (BOOT)
const int NUM_LEDS = 1;

// =====================================================================================
// =                               ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ                               =
// =====================================================================================

CRGB leds[NUM_LEDS];
WiFiClientSecure secured_client;
ConfigManager configManager;
BuzzerManager buzzer;
WebServer server(80);
DNSServer dnsServer;

// =====================================================================================
// =                               ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ                               =
// =====================================================================================

// Тайминги
const unsigned long POWER_CHECK_INTERVAL = 500;    // Проверка пина каждые 500 мс
const int DEBOUNCE_THRESHOLD = 5;                   
const unsigned long CONFIG_HOLD_TIME = 2000;       // Время удержания для входа в настройки

// Состояние питания
bool isPowerOn = false;
bool lastPowerState = false;
unsigned long lastPowerCheckTime = 0;
int debounceCounter = 0;
bool potentialPowerState = false;

// Состояние Wi-Fi и Системы
bool isWifiConnected = false;
bool isTimeSynced = false;
bool startupMessageSent = false;  
bool ntpSyncStarted = false;
unsigned long lastNTPCheckTime = 0;
bool needsStartupMessage = false;
bool isConfigMode = false;

// Индикация (двойной "пых" раз в 2 секунды)
unsigned long lastBlinkCycleTime = 0;
const unsigned long BLINK_INTERVAL = 2000;

// Управление кнопкой
unsigned long buttonPressStartTime = 0;
bool isButtonPressed = false;

// =====================================================================================
// =                              ОСНОВНЫЕ ФУНКЦИИ                                     =
// =====================================================================================

void setRGBColor(CRGB color) {
    leds[0] = color;
    FastLED.show();
}

void playMelody(const char* melodyFile, bool powerOn) {
    if (melodyFile != nullptr && strlen(melodyFile) > 0) {
        String filePath = "/sounds/";
        filePath += melodyFile;

        File file = LittleFS.open(filePath, "r");
        if (file) {
            String rtttl = "";
            while (file.available()) {
                String line = file.readStringUntil('\n');
                line.trim();
                if (line.length() > 0 && !line.startsWith("#")) {
                    rtttl = line;
                    break;
                }
            }
            file.close();

            if (rtttl.length() > 0) {
                Serial.print("🎵 Играем мелодию из файла: ");
                Serial.println(filePath);
                buzzer.play(rtttl);
                return;
            }
        }
        Serial.print("ℹ️ Файл не найден: ");
        Serial.println(filePath);
    }

    // Фоллбэк: встроенные RTTTL-мелодии
    if (powerOn) {
        buzzer.play("StdOn:d=8,o=6,b=120:c,p,c,p,c,p,4c");
    } else {
        buzzer.play("StdOff:d=1,o=5,b=60:2c");
    }
}

// Асинхронная индикация статуса: короткий двойной "пых" раз в 2 секунды
void handleStatusIndicationAsync() {
    unsigned long currentMillis = millis();
    unsigned long elapsed = currentMillis - lastBlinkCycleTime;
    
    if (elapsed >= BLINK_INTERVAL) {
        lastBlinkCycleTime = currentMillis;
        elapsed = 0;
    }

    CRGB statusColor;
    if (isConfigMode) {
        statusColor = CRGB::Blue;
    } else if (isPowerOn) {
        statusColor = CRGB::Green;
    } else {
        statusColor = CRGB::Red;
    }

    // Логика двойной вспышки
    if (elapsed < 60) {
        setRGBColor(statusColor);
    } else if (elapsed < 160) {
        setRGBColor(CRGB::Black);
    } else if (elapsed < 220) {
        setRGBColor(statusColor);
    } else {
        setRGBColor(CRGB::Black);
    }
}

void checkNTPStatusAsync() {
    if (ntpSyncStarted && !isTimeSynced) {
        if (millis() - lastNTPCheckTime >= 1000) {
            lastNTPCheckTime = millis();
            time_t now = time(nullptr);
            if (now > 24 * 3600) {
                Serial.println("\n✅ Время синхронизировано!");
                isTimeSynced = true;
                ntpSyncStarted = false;
                if (!startupMessageSent) needsStartupMessage = true;
            }
        }
    }
}

bool sendTelegramMessage(String msg) {
    if (!isWifiConnected) {
        Serial.println("❌ Ошибка: WiFi не подключен. Сообщение пропущено.");
        return false;
    }
    if (!isTimeSynced) {
        Serial.println("❌ Ошибка: Время не синхронизировано (NTP). Сообщение пропущено.");
        return false;
    }
    
    Serial.print("📤 Отправка в Telegram: ");
    Serial.println(msg);

    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(configManager.getBotToken()) + "/sendMessage";
    
    http.begin(secured_client, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);
    
    StaticJsonDocument<512> doc;
    doc["chat_id"] = configManager.getChatID();
    doc["text"] = msg;
    String threadId = configManager.getThreadID();
    if (threadId.length() > 0 && threadId != "0" && threadId != "null") {
        doc["message_thread_id"] = threadId.toInt();
    }
    
    String payload;
    serializeJson(doc, payload);
    int httpCode = http.POST(payload);
    
    if (httpCode == 200) {
        Serial.println("✅ Сообщение успешно доставлено.");
    } else {
        Serial.print("❌ Ошибка отправки! HTTP код: ");
        Serial.println(httpCode);
        if (httpCode < 0) {
            Serial.print("   Причина: ");
            Serial.println(http.errorToString(httpCode).c_str());
        } else {
            String response = http.getString();
            Serial.print("   Ответ сервера: ");
            Serial.println(response);
        }
    }
    
    http.end();
    return (httpCode == 200);
}

// Обработчик веб-сервера
void handleRoot() {
    String html = WEB_PORTAL_HTML;
    html.replace("{{ssid}}", configManager.getSSID());
    html.replace("{{pass}}", configManager.getPass());
    html.replace("{{token}}", configManager.getBotToken());
    html.replace("{{chat}}", configManager.getChatID());
    html.replace("{{thread}}", configManager.getThreadID());
    html.replace("{{melody_on_options}}", SoundStorage::buildMelodyOptions(configManager.getMelodyOnFile()));
    html.replace("{{melody_off_options}}", SoundStorage::buildMelodyOptions(configManager.getMelodyOffFile()));
    html.replace("{{volume}}", String(configManager.getBuzzerVolume()));
    html.replace("{{msg_startup_power}}", configManager.getMsgStartupPower());
    html.replace("{{msg_startup_battery}}", configManager.getMsgStartupBattery());
    html.replace("{{msg_power_restored}}", configManager.getMsgPowerRestored());
    html.replace("{{msg_power_lost}}", configManager.getMsgPowerLost());

    server.send(200, "text/html", html);
}

void handleSave() {
    if (server.hasArg("ssid")) configManager.setSSID(server.arg("ssid"));
    if (server.hasArg("pass")) configManager.setPass(server.arg("pass"));
    if (server.hasArg("token")) configManager.setBotToken(server.arg("token"));
    if (server.hasArg("chat")) configManager.setChatID(server.arg("chat"));
    if (server.hasArg("thread")) configManager.setThreadID(server.arg("thread"));
    if (server.hasArg("melody_on")) configManager.setMelodyOnFile(server.arg("melody_on"));
    if (server.hasArg("melody_off")) configManager.setMelodyOffFile(server.arg("melody_off"));
    if (server.hasArg("msg_startup_power")) configManager.setMsgStartupPower(server.arg("msg_startup_power"));
    if (server.hasArg("msg_startup_battery")) configManager.setMsgStartupBattery(server.arg("msg_startup_battery"));
    if (server.hasArg("msg_power_restored")) configManager.setMsgPowerRestored(server.arg("msg_power_restored"));
    if (server.hasArg("msg_power_lost")) configManager.setMsgPowerLost(server.arg("msg_power_lost"));
    
    configManager.save();
    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
}

void handlePlay() {
    String file = server.arg("file");
    String rtttl = "";

    if (file.length() > 0) {
        String filePath = "/sounds/" + file;
        File f = LittleFS.open(filePath, "r");
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() > 0 && !line.startsWith("#")) {
                    rtttl = line;
                    break;
                }
            }
            f.close();
        }
    }

    if (rtttl.length() > 0) {
        Serial.print("🎵 Предпрослушивание: ");
        Serial.println(file);
        buzzer.play(rtttl);
    } else {
        buzzer.play("preview:d=4,o=6,b=200:4c");
    }

    server.send(200, "text/plain", "OK");
}

void handleVolume() {
    if (server.hasArg("vol")) {
        uint8_t vol = constrain(server.arg("vol").toInt(), 0, 100);
        buzzer.setVolume(vol);
        configManager.setBuzzerVolume(vol);
        configManager.save();
        Serial.print("🔊 Громкость установлена: ");
        Serial.print(vol);
        Serial.println("%");
    }
    server.send(200, "text/plain", "OK");
}

void enterConfigMode() {
    isConfigMode = true;
    Serial.println("🌐 Вход в режим настройки...");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 0, 11), IPAddress(192, 168, 0, 11), IPAddress(255, 255, 255, 0));
    WiFi.softAP("ESS Watcher", "12345678");
    
    dnsServer.start(53, "*", IPAddress(192, 168, 0, 11));
    
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/play", handlePlay);
    server.on("/volume", handleVolume);
    server.onNotFound(handleRoot); // Для Captive Portal
    server.begin();
    
    // Звук входа в режим WiFi
    String wifiSound = SoundStorage::getSystemSound("wifi_mode");
    if (wifiSound.length() > 0) {
        buzzer.play(wifiSound);
    } else {
        buzzer.play("wifi:d=4,o=6,b=400:4g");
    }
}

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            isWifiConnected = true;
            Serial.print("🌐 WiFi подключен! IP: ");
            Serial.println(WiFi.localIP());
            WiFi.setSleep(WIFI_PS_MIN_MODEM);
            if (!isTimeSynced && !ntpSyncStarted) {
                ntpSyncStarted = true;
                configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            }
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (isWifiConnected) {
                Serial.println("⚠️ WiFi соединение потеряно.");
            }
            Serial.print("ℹ️ Причина отключения: ");
            Serial.println(info.wifi_sta_disconnected.reason);
            isWifiConnected = false;
            if (!isConfigMode) WiFi.reconnect();
            break;
        default: break;
    }
}

void setup() {
    delay(2000); // Даем время USB-порту инициализироваться в системе
    Serial.begin(115200);
    Serial.println("\n\n====================================");
    Serial.println("🚀 ESS Watcher: СИСТЕМА ЗАПУЩЕНА");
    Serial.println("====================================\n");
    
    configManager.begin();
    SoundStorage::begin();
    SoundStorage::listSounds();
    
    pinMode(PIN_POWER_SENSE, INPUT_PULLDOWN);
    pinMode(PIN_CONFIG_BUTTON, INPUT_PULLUP);
    
    FastLED.addLeds<WS2812B, PIN_LED_DATA, GRB>(leds, NUM_LEDS);
    buzzer.begin(PIN_BUZZER, 0);
    buzzer.setVolume(configManager.getBuzzerVolume());
    
    secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    
    isPowerOn = digitalRead(PIN_POWER_SENSE) == HIGH;
    lastPowerState = isPowerOn;
    potentialPowerState = isPowerOn;

    // Звук включения устройства
    String startupSound = SoundStorage::getSystemSound("startup");
    if (startupSound.length() > 0) {
        buzzer.play(startupSound);
    } else {
        buzzer.play("startup:d=4,o=6,b=300:4b");
    }

    WiFi.onEvent(WiFiEvent);
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_5dBm); // Ограничиваем мощность СРАЗУ для стабильности питания
    WiFi.setAutoReconnect(true);
    WiFi.begin(configManager.getSSID(), configManager.getPass());
}

void loop() {
    if (isConfigMode) {
        dnsServer.processNextRequest();
        server.handleClient();
        buzzer.handleAsync();
        handleStatusIndicationAsync();
        return;
    }

    // 1. Асинхронные задачи
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 5000) {
        lastHeartbeat = millis();
        Serial.print("💓 Система работает... WiFi: ");
        Serial.print(isWifiConnected ? "OK " : "НЕТ ");
        Serial.print("| Сеть 220В: ");
        Serial.println(isPowerOn ? "ECTЬ" : "НЕТ");
    }

    checkNTPStatusAsync();
    buzzer.handleAsync();
    handleStatusIndicationAsync();

    // 2. Кнопка настройки
    bool btnState = digitalRead(PIN_CONFIG_BUTTON) == LOW;
    if (btnState && !isButtonPressed) {
        isButtonPressed = true;
        buttonPressStartTime = millis();
    } else if (!btnState && isButtonPressed) {
        isButtonPressed = false;
    }
    
    if (isButtonPressed && (millis() - buttonPressStartTime >= CONFIG_HOLD_TIME)) {
        enterConfigMode();
    }

    // 3. Стартовое сообщение
    if (needsStartupMessage) {
        needsStartupMessage = false;
        startupMessageSent = true;
        if (isPowerOn) {
            sendTelegramMessage(configManager.getMsgStartupPower());
        } else {
            sendTelegramMessage(configManager.getMsgStartupBattery());
        }
    }

    // 4. Мониторинг питания
    if (millis() - lastPowerCheckTime >= POWER_CHECK_INTERVAL) {
        lastPowerCheckTime = millis();
        bool currentReading = digitalRead(PIN_POWER_SENSE) == HIGH;

        if (currentReading == potentialPowerState) debounceCounter++;
        else { debounceCounter = 0; potentialPowerState = currentReading; }

        if (debounceCounter >= DEBOUNCE_THRESHOLD) {
            if (potentialPowerState != lastPowerState) {
                lastPowerState = potentialPowerState;
                isPowerOn = potentialPowerState;

                if (isPowerOn) {
                    Serial.println("⚡ Питание: СЕТЬ 220В ВОССТАНОВЛЕНА");
                    playMelody(configManager.getMelodyOnFile(), true);
                    sendTelegramMessage(configManager.getMsgPowerRestored());
                } else {
                    Serial.println("🚨 Питание: ОТКЛЮЧЕНИЕ 220В!");
                    playMelody(configManager.getMelodyOffFile(), false);
                    sendTelegramMessage(configManager.getMsgPowerLost());
                }
            }
        }
    }
    delay(10);
}
