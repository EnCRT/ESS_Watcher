#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <time.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>
#include "secrets.h"

// =====================================================================================
// =                               НАСТРОЙКИ ПИНОВ                                     =
// =====================================================================================

const int PIN_POWER_SENSE = 0; // Пин для мониторинга 3.3В (через делитель на 3.3В!) от блока питания 220В
const int PIN_BUZZER = 2;      // Пин для зуммера
const int PIN_LED_DATA = 10;   // Пин для WS2812B
const int NUM_LEDS = 1;

CRGB leds[NUM_LEDS];
WiFiClientSecure secured_client;

// =====================================================================================
// =                               НАСТРОЙКИ СЕТИ И TELEGRAM                         =
// =====================================================================================
const char* WIFI_SSID = SECRET_WIFI_SSID;
const char* WIFI_PASSWORD = SECRET_WIFI_PASSWORD;
const String BOT_TOKEN = SECRET_BOT_TOKEN;
const String CHAT_ID = SECRET_CHAT_ID;
const String MESSAGE_THREAD_ID = SECRET_MESSAGE_THREAD_ID;

// =====================================================================================
// =                               ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ                               =
// =====================================================================================

// Тайминги
const unsigned long POWER_CHECK_INTERVAL = 500;    // Проверка пина каждые 500 мс (для антидребезга)
const unsigned long BUFFER_PROCESS_INTERVAL = 10000; // Попытка отправки из буфера каждые 10 сек
const int DEBOUNCE_THRESHOLD = 5;                   // Сколько раз подряд (по 100мс) статус должен быть стабильным

// Состояние питания
bool isPowerOn = false;
bool lastPowerState = false;
unsigned long lastPowerCheckTime = 0;

// Переменные антидребезга (Debounce)
int debounceCounter = 0;
bool potentialPowerState = false;

// Состояние Wi-Fi подключения
bool wifiEnabled = true;          
bool isWifiConnected = false;
bool isTimeSynced = false;
bool startupMessageSent = false;  

// Асинхронные флаги
bool ntpSyncStarted = false;
unsigned long lastNTPCheckTime = 0;
bool needsStartupMessage = false;

unsigned long lastBufferProcessTime = 0;

// Буфер сообщений
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

void setRGBColor(CRGB color) {
    leds[0] = color;
    FastLED.show();
}

void startBuzzer(int freq, unsigned long duration) {
    buzzerFreq = freq;
    buzzerDuration = duration;
    buzzerStartTime = millis();
    isBuzzerActive = true;
    
    // Совместимость с ядром ESP32 версии 2.x
    ledcSetup(0, freq, 8); // Канал 0, нужная частота, разрешение 8 бит
    ledcAttachPin(PIN_BUZZER, 0);
    ledcWriteTone(0, freq);
}

void stopBuzzer() {
    isBuzzerActive = false;
    ledcWriteTone(0, 0); // Отключаем звук
    ledcDetachPin(PIN_BUZZER);
}

void handleBuzzerAsync() {
    if (isBuzzerActive && (millis() - buzzerStartTime >= buzzerDuration)) {
        stopBuzzer();
    }
}

// Асинхронная проверка статуса NTP
void checkNTPStatusAsync() {
    if (ntpSyncStarted && !isTimeSynced) {
        if (millis() - lastNTPCheckTime >= 1000) {
            lastNTPCheckTime = millis();
            time_t now = time(nullptr);
            if (now > 24 * 3600) {
                Serial.println("\n✅ Время успешно синхронизировано!");
                isTimeSynced = true;
                ntpSyncStarted = false;
                
                if (!startupMessageSent) {
                    needsStartupMessage = true;
                }
            } else {
                Serial.print(".");
            }
        }
    }
}

bool sendTelegramMessage(String msg) {
    if (!wifiEnabled || !isWifiConnected || !isTimeSynced) return false;
    
    Serial.println("🔄 Отправка запроса в Telegram...");
    
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + BOT_TOKEN + "/sendMessage";
    
    // Используем уже настроенный secured_client с сертификатами
    http.begin(secured_client, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);
    
    // Формируем правильный JSON с поддержкой топиков (message_thread_id)
    StaticJsonDocument<512> doc;
    doc["chat_id"] = CHAT_ID;
    doc["text"] = msg;
    if (MESSAGE_THREAD_ID.length() > 0 && MESSAGE_THREAD_ID != "null") {
        doc["message_thread_id"] = MESSAGE_THREAD_ID.toInt();
    }
    
    String payload;
    serializeJson(doc, payload);
    
    // Отправляем ОДНИМ большим пакетом (решает проблему перегрева и спама мелких SSL чанков)
    int httpCode = http.POST(payload);
    bool success = (httpCode == 200);
    
    if (success) {
        Serial.println("✅ Telegram: Сообщение успешно отправлено!");
    } else {
        Serial.printf("❌ Telegram: Ошибка отправки! HTTP Код: %d\n", httpCode);
        String response = http.getString();
        Serial.println("Ответ сервера Telegram: " + response);
    }
    
    http.end();
    return success;
}

void addToBuffer(String msg) {
    if (bufferCount < MAX_BUFFER_SIZE) {
        messageBuffer[bufferCount] = msg;
        bufferCount++;
        Serial.println("📥 Сообщение добавлено в буфер.");
    } else {
        Serial.println("⚠️ Буфер переполнен! Старое сообщение удалено.");
        // Сдвигаем старые сообщения
        for (int i = 1; i < MAX_BUFFER_SIZE; i++) {
            messageBuffer[i-1] = messageBuffer[i];
        }
        messageBuffer[MAX_BUFFER_SIZE-1] = msg;
    }
}

void processMessageBuffer() {
    if (bufferCount > 0 && isWifiConnected && isTimeSynced) {
        if (millis() - lastBufferProcessTime >= BUFFER_PROCESS_INTERVAL) {
            lastBufferProcessTime = millis();
            
            Serial.printf("📦 Отправка буфера (сообщений: %d)...\n", bufferCount);
            if (sendTelegramMessage(messageBuffer[0])) {
                // Если успешно отправлено, удаляем из буфера
                for (int i = 1; i < bufferCount; i++) {
                    messageBuffer[i-1] = messageBuffer[i];
                }
                bufferCount--;
                Serial.printf("✅ Сообщение из буфера ушло. Осталось: %d\n", bufferCount);
            } else {
                Serial.println("⏳ Ошибка отправки из буфера. Повтор позже.");
            }
        }
    }
}

// Обработчик событий Wi-Fi
void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("📶✅ Подключено к Wi-Fi! IP адрес: ");
            Serial.println(WiFi.localIP());
            isWifiConnected = true;
            
            // Снижаем мощность передатчика ЗДЕСЬ (после коннекта), чтобы она не сбрасывалась
            WiFi.setTxPower(WIFI_POWER_8_5dBm);
            // Включаем энергосбережение модема
            WiFi.setSleep(WIFI_PS_MIN_MODEM);
            
            // Запускаем асинхронную синхронизацию времени
            if (!isTimeSynced && !ntpSyncStarted) {
                ntpSyncStarted = true;
                configTime(0, 0, "pool.ntp.org", "time.nist.gov", "162.159.200.1");
                Serial.print("🔄 Синхронизация времени NTP");
            }
            break;
            
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("📶❌ Wi-Fi отключен! Попытка переподключения...");
            isWifiConnected = false;
            WiFi.reconnect();
            break;
            
        default:
            break;
    }
}

// =====================================================================================
// =                                  SETUP & LOOP                                     =
// =====================================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== Запуск ESS Watcher (Pro Edition) ===");

    // Оптимизация памяти: резервируем место под строковые объекты в куче
    for (int i = 0; i < MAX_BUFFER_SIZE; i++) {
        messageBuffer[i].reserve(256);
    }

    pinMode(PIN_POWER_SENSE, INPUT_PULLDOWN);

    // Инициализация WS2812B
    FastLED.addLeds<WS2812B, PIN_LED_DATA, GRB>(leds, NUM_LEDS);
    setRGBColor(CRGB::Yellow); // Желтый - запуск

    // Настройка SSL
    secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

    // Первичное считывание пина питания (сразу устанавливаем стейт)
    isPowerOn = digitalRead(PIN_POWER_SENSE) == HIGH;
    lastPowerState = isPowerOn;
    potentialPowerState = isPowerOn;

    if (wifiEnabled) {
        Serial.println("Подключение к Wi-Fi: " + String(WIFI_SSID));
        
        WiFi.onEvent(WiFiEvent);
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    } else {
        Serial.println("Wi-Fi отключен в настройках.");
    }
}

void loop() {
    // 1. Асинхронные сервисные задачи
    checkNTPStatusAsync();
    handleBuzzerAsync();
    processMessageBuffer();

    // 2. Отложенная отправка стартового сообщения (когда NTP готов)
    if (needsStartupMessage) {
        needsStartupMessage = false;
        startupMessageSent = true;
        
        if (isPowerOn) {
            sendTelegramMessage("🚀 ⚡️ ✅ Датчик и сеть 220В подключена!");
            setRGBColor(CRGB::Green);
        } else {
            sendTelegramMessage("🚀 ❌ ⚠️ Датчик от АКБ, сеть 220В ОТКЛЮЧЕНА!");
            setRGBColor(CRGB::Red);
        }
    }

    // 3. Аппаратный мониторинг пина с АНТИДРЕБЕЗГОМ (Debounce)
    if (millis() - lastPowerCheckTime >= POWER_CHECK_INTERVAL) {
        lastPowerCheckTime = millis();
        bool currentReading = digitalRead(PIN_POWER_SENSE) == HIGH;

        if (currentReading == potentialPowerState) {
            debounceCounter++;
        } else {
            debounceCounter = 0;
            potentialPowerState = currentReading;
        }

        // Если состояние стабильно на протяжении DEBOUNCE_THRESHOLD чтений
        if (debounceCounter >= DEBOUNCE_THRESHOLD) {
            if (potentialPowerState != lastPowerState) {
                lastPowerState = potentialPowerState;
                isPowerOn = potentialPowerState;

                if (isPowerOn) {
                    Serial.println("⚡ Сеть 220В ВОССТАНОВЛЕНА!");
                    setRGBColor(CRGB::Green);
                    startBuzzer(2000, 200); // Короткий писк
                    
                    if (!sendTelegramMessage("⚡ Сеть 220В ВОССТАНОВЛЕНА!")) {
                        addToBuffer("⚡ Сеть 220В ВОССТАНОВЛЕНА!");
                    }
                } else {
                    Serial.println("⚠️ Сеть 220В ОТКЛЮЧЕНА!");
                    setRGBColor(CRGB::Red);
                    startBuzzer(1000, 1000); // Длинный гудок
                    
                    if (!sendTelegramMessage("🚨 ОТКЛЮЧЕНИЕ ПИТАНИЯ 220В! Работа от АКБ 🔋.")) {
                        addToBuffer("🚨 ОТКЛЮЧЕНИЕ ПИТАНИЯ 220В! Работа от АКБ 🔋");
                    }
                }
            }
        }
    }

    // 4. ЭНЕРГОСБЕРЕЖЕНИЕ (Освобождение CPU для FreeRTOS Idle Task)
    // Эта микро-пауза позволяет ядру перевести чип в Light Sleep,
    // радикально снижая энергопотребление и нагрев.
    delay(200);
}
