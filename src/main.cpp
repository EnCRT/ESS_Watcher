#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include "secrets.h"

// =====================================================================================
// =                                 БЛОК КОНФИГУРАЦИИ                                 =
// =====================================================================================

// --- Настройки Wi-Fi ---
const char* WIFI_SSID = SECRET_WIFI_SSID;             // Имя вашей Wi-Fi сети
const char* WIFI_PASSWORD = SECRET_WIFI_PASSWORD;       // Пароль от вашей Wi-Fi сети

// --- Настройки Telegram ---
const String BOT_TOKEN = SECRET_BOT_TOKEN;           // Токен, полученный от @BotFather
const String CHAT_ID = SECRET_CHAT_ID;                // ID вашего чата для уведомлений
const String MESSAGE_THREAD_ID = SECRET_MESSAGE_THREAD_ID;                 // ID вашей темы для уведомлений

// --- Настройки пинов (Hardware Pins) ---
#define PIN_POWER_SENSE  0   // Пин для мониторинга 220В (через делитель напряжения от 5В)
#define PIN_BUZZER       2   // Пин для подключения пассивного зуммера
#define PIN_WS2812B      10  // Пин данных для внешнего RGB светодиода WS2812B
#define PIN_ONBOARD_LED  8   // Встроенный синий светодиод на плате ESP32-C3 SuperMini

// --- Настройки WS2812B ---
#define NUM_LEDS         1   // Количество светодиодов в ленте/модуле

// --- Тайминги и интервалы ---
const unsigned long POWER_CHECK_INTERVAL = 2000;     // Интервал опроса пина наличия сети (в миллисекундах)
const unsigned long WIFI_RECONNECT_INTERVAL = 20000; // Интервал попыток переподключения к Wi-Fi (в миллисекундах)
const unsigned long WIFI_LED_BLINK_INTERVAL = 166;   // Интервал мигания бортового светодиода (3 раза в сек = ~166мс on/off)

// =====================================================================================
// =                              ГЛОБАЛЬНЫЕ ОБЪЕКТЫ                                   =
// =====================================================================================

CRGB leds[NUM_LEDS];
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// =====================================================================================
// =                              ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ                                =
// =====================================================================================

// Состояние сети 220В
bool isPowerOn = false;           // Текущее состояние питания
bool lastPowerState = false;      // Предыдущее состояние (для отслеживания изменений)
unsigned long lastPowerCheckTime = 0;

// Состояние Wi-Fi индикации
unsigned long lastWifiLedTime = 0;
bool wifiLedState = HIGH;         // Помним, что бортовой светодиод инвертирован (HIGH = ВЫКЛ, LOW = ВКЛ)

// Состояние Wi-Fi подключения
unsigned long lastWifiCheckTime = 0;
bool wifiEnabled = true;          // Флаг активности Wi-Fi
bool startupMessageSent = false;  // Флаг отправки стартового сообщения

// Буфер сообщений (если сеть недоступна)
const int MAX_BUFFER_SIZE = 5;
String messageBuffer[MAX_BUFFER_SIZE];
int bufferCount = 0;

// Управление зуммером
bool isBuzzerActive = false;
unsigned long buzzerStartTime = 0;
unsigned long buzzerDuration = 0;
int buzzerFreq = 0;

// =====================================================================================
// =                              ОСНОВНЫЕ ФУНКЦИИ                                     =
// =====================================================================================

void addToBuffer(String msg) {
    if (!wifiEnabled) return;
    if (bufferCount < MAX_BUFFER_SIZE) {
        messageBuffer[bufferCount] = msg;
        bufferCount++;
    } else {
        // Сдвигаем старые сообщения, если буфер переполнен (FIFO)
        for (int i = 1; i < MAX_BUFFER_SIZE; i++) {
            messageBuffer[i-1] = messageBuffer[i];
        }
        messageBuffer[MAX_BUFFER_SIZE-1] = msg;
    }
}

void sendTelegramMessage(String msg) {
    if (!wifiEnabled) return;
    if (MESSAGE_THREAD_ID.length() > 0 && MESSAGE_THREAD_ID != "null") {
        bot.sendMessage(CHAT_ID, msg, "", MESSAGE_THREAD_ID.toInt());
    } else {
        bot.sendMessage(CHAT_ID, msg, "");
    }
}

void processMessageBuffer() {
    if (!wifiEnabled) return;
    if (WiFi.status() == WL_CONNECTED && bufferCount > 0) {
        for (int i = 0; i < bufferCount; i++) {
            sendTelegramMessage(messageBuffer[i]);
            Serial.println("Отправлено из буфера: " + messageBuffer[i]);
        }
        bufferCount = 0; // Очищаем буфер после успешной отправки
    }
}

void setRGBColor(CRGB color) {
    leds[0] = color;
    FastLED.show();
}

void startBuzzer(int freq, unsigned long duration) {
    buzzerFreq = freq;
    buzzerDuration = duration;
    buzzerStartTime = millis();
    isBuzzerActive = true;
    tone(PIN_BUZZER, freq);
}

void stopBuzzer() {
    isBuzzerActive = false;
    noTone(PIN_BUZZER);
}

void handleBuzzerAsync() {
    if (isBuzzerActive) {
        if (millis() - buzzerStartTime >= buzzerDuration) {
            stopBuzzer();
        }
    }
}

void checkWiFi() {
    if (!wifiEnabled) return;

    // Индикация Wi-Fi бортовым светодиодом
    if (WiFi.status() != WL_CONNECTED) {
        // Мигаем (поиск сети)
        if (millis() - lastWifiLedTime >= WIFI_LED_BLINK_INTERVAL) {
            lastWifiLedTime = millis();
            wifiLedState = !wifiLedState;
            digitalWrite(PIN_ONBOARD_LED, wifiLedState);
        }
        
        // Попытка переподключения каждые WIFI_RECONNECT_INTERVAL
        if (millis() - lastWifiCheckTime >= WIFI_RECONNECT_INTERVAL) {
            lastWifiCheckTime = millis();
            Serial.println("Попытка переподключения к Wi-Fi...");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    } else {
        // Подключено (постоянное свечение, LOW = ВКЛ для инвертированного LED)
        digitalWrite(PIN_ONBOARD_LED, LOW);
        
        // Отправка стартового сообщения при первом успешном подключении
        if (!startupMessageSent) {
            startupMessageSent = true;
            Serial.println("Устройство подключено к Wi-Fi. Отправка стартового сообщения.");
            sendTelegramMessage("🔌 Устройство включено, питание восстановлено!");
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000); // Даем время Serial на инициализацию
    Serial.println("\n=== Запуск ESS Watcher ===");

    // Настройка пинов
    pinMode(PIN_POWER_SENSE, INPUT); // PULLUP не нужен, используется внешний делитель
    pinMode(PIN_ONBOARD_LED, OUTPUT);
    digitalWrite(PIN_ONBOARD_LED, HIGH); // Выключаем (инвертировано)

    // Инициализация WS2812B
    FastLED.addLeds<WS2812B, PIN_WS2812B, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(100);
    setRGBColor(CRGB::Black); // Выключаем при старте

    // Инициализация Wi-Fi
    if (strlen(WIFI_SSID) == 0 || strlen(WIFI_PASSWORD) == 0) {
        wifiEnabled = false;
        Serial.println("Wi-Fi отключен (пустые SSID или Password)");
    } else {
        Serial.print("Подключение к Wi-Fi: ");
        Serial.println(WIFI_SSID);
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Настройка сертификата для Telegram
    }
    
    // Считываем начальное состояние сети
    isPowerOn = digitalRead(PIN_POWER_SENSE) == HIGH;
    lastPowerState = isPowerOn;
    
    if (isPowerOn) {
        setRGBColor(CRGB::Green);
    } else {
        setRGBColor(CRGB::Red);
    }
}

void loop() {
    handleBuzzerAsync();
    checkWiFi();
    processMessageBuffer();

    // Опрос датчика напряжения каждые POWER_CHECK_INTERVAL
    if (millis() - lastPowerCheckTime >= POWER_CHECK_INTERVAL) {
        lastPowerCheckTime = millis();
        
        isPowerOn = digitalRead(PIN_POWER_SENSE) == HIGH;

        // Если состояние изменилось
        if (isPowerOn != lastPowerState) {
            lastPowerState = isPowerOn;

            if (isPowerOn) {
                // Питание появилось
                Serial.println("⚡ Сеть 220В ВОССТАНОВЛЕНА!");
                setRGBColor(CRGB::Green);
                startBuzzer(2000, 500); // Короткий сигнал высокой тональности
                
                String msg = "✅ Питание восстановлено.";
                if (WiFi.status() == WL_CONNECTED) {
                    sendTelegramMessage(msg);
                } else {
                    addToBuffer(msg);
                }
            } else {
                // Питание пропало
                Serial.println("⚠️ Сеть 220В ОТКЛЮЧЕНА!");
                setRGBColor(CRGB::Red);
                startBuzzer(800, 5000); // Длинный тревожный сигнал низкой тональности (5 сек)
                
                String msg = "🚨 ОТКЛЮЧЕНИЕ ПИТАНИЯ 220В!";
                if (WiFi.status() == WL_CONNECTED) {
                    sendTelegramMessage(msg);
                } else {
                    addToBuffer(msg);
                }
            }
        }
    }
}
