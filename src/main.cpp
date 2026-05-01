#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <time.h>
#include <HTTPClient.h>
#include "secrets.h"

// =====================================================================================
// =                                 БЛОК КОНФИГУРАЦИИ                                 =
// =====================================================================================

// --- Настройки Wi-Fi ---
const char* WIFI_SSID = SECRET_WIFI_SSID;
const char* WIFI_PASSWORD = SECRET_WIFI_PASSWORD;

// --- Настройки Telegram ---
const String BOT_TOKEN = SECRET_BOT_TOKEN;
const String CHAT_ID = SECRET_CHAT_ID;
const String MESSAGE_THREAD_ID = SECRET_MESSAGE_THREAD_ID;

// --- Настройки пинов (Hardware Pins) ---
#define PIN_POWER_SENSE  0   // Пин для мониторинга 220В (через делитель напряжения от 5В)
#define PIN_BUZZER       2   // Пин для подключения пассивного зуммера
#define PIN_WS2812B      10  // Пин данных для внешнего RGB светодиода WS2812B
// Бортовой светодиод (GPIO 8) УДАЛЕН из логики для избежания проблем при загрузке.

// --- Настройки WS2812B ---
#define NUM_LEDS         1   // Количество светодиодов в ленте/модуле

// --- Тайминги и интервалы ---
const unsigned long POWER_CHECK_INTERVAL = 2000;     // Интервал опроса пина наличия сети (мс)
const unsigned long BUFFER_PROCESS_INTERVAL = 5000;  // Интервал попыток отправки буфера (мс)

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
bool isPowerOn = false;           
bool lastPowerState = false;      
unsigned long lastPowerCheckTime = 0;

// Состояние Wi-Fi подключения
bool wifiEnabled = true;          
bool isWifiConnected = false;
bool isTimeSynced = false;
bool startupMessageSent = false;  

bool needsTimeSync = false;
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
    if (isBuzzerActive) {
        if (millis() - buzzerStartTime >= buzzerDuration) {
            stopBuzzer();
        }
    }
}

// =====================================================================================
// =                              TELEGRAM & СЕТЬ                                      =
// =====================================================================================

void syncTime() {
    // Используем как доменные имена, так и прямые IP (Cloudflare NTP), если DNS все еще сбоит
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "162.159.200.1");
    Serial.print("Синхронизация времени NTP");
    time_t now = time(nullptr);
    int retries = 0;
    while (now < 24 * 3600 && retries < 15) {
        Serial.print(".");
        delay(500);
        now = time(nullptr);
        retries++;
    }
    if (now > 24 * 3600) {
        Serial.println("\n✅ Время успешно синхронизировано!");
        isTimeSynced = true;
    } else {
        Serial.println("\n❌ Ошибка синхронизации времени.");
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
    if (!wifiEnabled) return;
    if (bufferCount < MAX_BUFFER_SIZE) {
        messageBuffer[bufferCount] = msg;
        bufferCount++;
        Serial.println("📥 Сообщение добавлено в буфер.");
    } else {
        // Сдвигаем старые сообщения, если буфер переполнен (FIFO)
        for (int i = 1; i < MAX_BUFFER_SIZE; i++) {
            messageBuffer[i-1] = messageBuffer[i];
        }
        messageBuffer[MAX_BUFFER_SIZE-1] = msg;
        Serial.println("📥 Сообщение добавлено в буфер (старое удалено).");
    }
}

void processMessageBuffer() {
    if (!wifiEnabled || !isWifiConnected || !isTimeSynced || bufferCount == 0) return;
    
    if (millis() - lastBufferProcessTime >= BUFFER_PROCESS_INTERVAL) {
        lastBufferProcessTime = millis();
        
        Serial.printf("📦 Отправка буфера (сообщений: %d)...\n", bufferCount);
        
        // Пытаемся отправить только самое старое (нулевое) сообщение
        if (sendTelegramMessage(messageBuffer[0])) {
            // Если успешно, сдвигаем очередь влево
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

// Обработчик событий Wi-Fi
void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("✅ Подключено к Wi-Fi! IP адрес: ");
            Serial.println(WiFi.localIP());
            isWifiConnected = true;
            
            // Снижаем мощность передатчика ЗДЕСЬ (после коннекта), чтобы она не сбрасывалась
            WiFi.setTxPower(WIFI_POWER_8_5dBm);
            // Включаем энергосбережение модема
            WiFi.setSleep(WIFI_PS_MIN_MODEM);
            
            // Ставим флаг, чтобы синхронизировать время в основном цикле
            if (!isTimeSynced) {
                needsTimeSync = true;
            }
            break;
            
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("❌ Wi-Fi отключен! Попытка переподключения...");
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
    Serial.println("\n=== Запуск ESS Watcher (Pro Edition) ===");

    // Настройка пинов
    pinMode(PIN_POWER_SENSE, INPUT); 

    // Инициализация WS2812B
    FastLED.addLeds<WS2812B, PIN_WS2812B, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(100);
    setRGBColor(CRGB::Black); 

    // Инициализация Wi-Fi
    if (strlen(WIFI_SSID) == 0 || strlen(WIFI_PASSWORD) == 0 || String(WIFI_SSID) == "0") {
        wifiEnabled = false;
        Serial.println("⚠️ Wi-Fi отключен (пустые SSID или Password)");
    } else {
        Serial.print("Подключение к Wi-Fi: ");
        Serial.println(WIFI_SSID);
        
        // Подписываемся на события Wi-Fi
        WiFi.onEvent(WiFiEvent);
        
        // Запуск Wi-Fi в режиме клиента
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        
        // Настройка сертификата Telegram
        secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT); 
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
    // Безопасное выполнение отложенных задач из Wi-Fi событий
    if (needsTimeSync) {
        needsTimeSync = false;
        syncTime();
        if (isTimeSynced && !startupMessageSent) {
            needsStartupMessage = true;
        }
    }

    if (needsStartupMessage) {
        needsStartupMessage = false;
        startupMessageSent = true;
        sendTelegramMessage("🔌 Устройство включено, сеть подключена!");
    }

    handleBuzzerAsync();
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
                startBuzzer(2000, 500); 
                
                String msg = "✅ Питание восстановлено.";
                if (!sendTelegramMessage(msg)) {
                    addToBuffer(msg);
                }
            } else {
                // Питание пропало
                Serial.println("⚠️ Сеть 220В ОТКЛЮЧЕНА!");
                setRGBColor(CRGB::Red);
                startBuzzer(800, 5000); 
                
                String msg = "🚨 ОТКЛЮЧЕНИЕ ПИТАНИЯ 220В!";
                if (!sendTelegramMessage(msg)) {
                    addToBuffer(msg);
                }
            }
        }
    }
}
