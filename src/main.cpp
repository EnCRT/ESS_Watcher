#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <time.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "esp_bt.h"
#include "secrets.h"
#include "ConfigManager.h"
#include "WebPortal.h"
#include "BuzzerManager.h"
#include "SoundStorage.h"

// =====================================================================================
// =                              CORE FUNCTIONS                                        =
// =====================================================================================

bool isQuietHours(); // forward declaration (используется в checkNTPStatusAsync)

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

// §5.5: мягкий старт — отсрочка тяжёлой периферии на 2 с после старта (даёт стабилизироваться boost-конвертеру UPS).
//       Реализована через millis() (НЕ delay), чтобы CPU мог уйти в IDLE и не жечь ток.
const unsigned long SOFT_START_DELAY_MS = 2000;
// §3.1/§5.6: задержка перед поднятием WiFi и стартовым сообщением (3–5 с после soft-start).
const unsigned long WIFI_STARTUP_DELAY_MS = 4000;
// §2.3: при смене статуса сети светодиод полностью выключается на 3 с (чтобы не добавлять пиковый ток в момент события)
const unsigned long LED_BLACKOUT_AFTER_EVENT_MS = 3000;

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
bool isConfigMode = false;

// §2.1/§5.6: отложенное сообщение — отправляется ПОСЛЕ завершения мелодии (анти-коллизия тока).
// В startup-фазе сюда кладётся стартовое/событийное сообщение, в loop — при смене статуса.
String pendingMessage = "";
// §2.1: источник pendingMessage — ждать ли конца мелодии перед отправкой
bool pendingMessageAwaitMelody = false;

// §5.5: фаза запуска устройства (мягкий старт через millis)
unsigned long bootTime = 0;             // millis() в момент выхода из setup()
bool softStartComplete = false;         // прошёл ли мягкий старт (тяжёлая периферия включена)

// §3.1: lazy WiFi — WiFi поднимается не сразу, а по истечении WIFI_STARTUP_DELAY_MS
bool wifiStarted = false;

// §2.3: тайминг «затемнения» LED после события сети
unsigned long ledBlackoutUntil = 0;     // до этого момента LED принудительно выключен

// Индикация (двойной "пых" раз в 2 секунды)
unsigned long lastBlinkCycleTime = 0;
const unsigned long BLINK_INTERVAL = 2000;

// §2.4: кэш последнего отправленного в WS2812B цвета (чтобы не дёргать FastLED.show() без изменений)
CRGB lastSentColor = CRGB::Black;
bool ledInited = false;

// Управление кнопкой
unsigned long buttonPressStartTime = 0;
bool isButtonPressed = false;

// =====================================================================================
// =                              ОСНОВНЫЕ ФУНКЦИИ                                     =
// =====================================================================================

void setRGBColor(CRGB color) {
    // §2.4: передаём в WS2812B только при изменении — экономим SPI/DMA и ток.
    if (!ledInited || leds[0] != color || lastSentColor != color) {
        leds[0] = color;
        FastLED.show();
        lastSentColor = color;
        ledInited = true;
    }
}

void setRGBColorForced(CRGB color) {
    // Принудительная передача (для режимов, где состояние важно гарантированно выставить)
    leds[0] = color;
    FastLED.show();
    lastSentColor = color;
    ledInited = true;
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
    // §2.3: затемнение LED после смены статуса сети — на это время LED полностью выключен.
    if (millis() < ledBlackoutUntil) {
        setRGBColor(CRGB::Black);
        return;
    }

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

// §2.1/§3.1/§5.6: постановка сообщения в очередь (с возможностью дождаться мелодии).
//   Используется и для стартового сообщения, и для события сети.
void queueMessage(const String& msg, bool awaitMelody) {
    pendingMessage = msg;
    pendingMessageAwaitMelody = awaitMelody;
}

// §5.6: проверка — был ли ресет после brownout во время работы от сети?
//   Сравниваем последнее подтверждённое состояние (из NVS) с текущим чтением пина.
bool isRebootAfterBrownout() {
    bool saved = configManager.getLastKnownPowerState();
    bool now = digitalRead(PIN_POWER_SENSE) == HIGH;
    // Если сохранено «сеть была», а сейчас пин LOW — это switchover-ресет (сеть только что пропала).
    return saved && !now;
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
                // §5.6: стартовое сообщение готовим только если это НЕ brownout-ресет.
                //   При brownout вместо «startup_battery» уйдёт «power_lost» (см. setup()).
                if (!startupMessageSent) {
                    // Если событие сети уже поставило сообщение в очередь — стартовое не нужно
                    if (pendingMessage.length() > 0) {
                        Serial.println("🔕 Стартовое сообщение пропущено: уже есть событие сети в очереди.");
                        startupMessageSent = true;
                    } else if (isRebootAfterBrownout()) {
                        Serial.println("🔔 Обнаружен brownout-ресет: сеть 220В пропала. Готовлю событие «power lost».");
                        if (!isQuietHours()) {
                            playMelody(configManager.getMelodyOffFile(), false);
                            queueMessage(configManager.getMsgPowerLost(), /*awaitMelody=*/true);
                        } else {
                            queueMessage(configManager.getMsgPowerLost(), /*awaitMelody=*/false);
                        }
                        startupMessageSent = true;
                    } else {
                        bool awaitMelody = buzzer.playing();
                        queueMessage(isPowerOn ? configManager.getMsgStartupPower()
                                               : configManager.getMsgStartupBattery(),
                                     awaitMelody);
                        startupMessageSent = true;
                    }
                }
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

    // §6.3: TLS-хэндшейк требует высокой частоты — поднимаем CPU на время запроса.
    setCpuFrequencyMhz(160);

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

    // §6.3: возвращаем экономную частоту
    setCpuFrequencyMhz(80);
    return (httpCode == 200);
}

// Проверка: сейчас тихий час? (мелодии не воспроизводятся)
bool isQuietHours() {
    if (!configManager.getQuietHoursEnabled()) return false;
    if (!isTimeSynced) return false;

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    int currentMinutes = t->tm_hour * 60 + t->tm_min;

    int startMinutes = configManager.getQuietStartHour() * 60 + configManager.getQuietStartMinute();
    int endMinutes   = configManager.getQuietEndHour()   * 60 + configManager.getQuietEndMinute();

    if (startMinutes < endMinutes) {
        return currentMinutes >= startMinutes && currentMinutes < endMinutes;
    } else {
        return currentMinutes >= startMinutes || currentMinutes < endMinutes;
    }
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
    html.replace("{{quiet_checked}}", configManager.getQuietHoursEnabled() ? "checked" : "");
    html.replace("{{quiet_start_hour}}", String(configManager.getQuietStartHour()));
    html.replace("{{quiet_start_minute}}", String(configManager.getQuietStartMinute()));
    html.replace("{{quiet_end_hour}}", String(configManager.getQuietEndHour()));
    html.replace("{{quiet_end_minute}}", String(configManager.getQuietEndMinute()));

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
    configManager.setQuietHoursEnabled(server.hasArg("quiet_enabled"));
    if (server.hasArg("quiet_start_hour")) configManager.setQuietStartHour(server.arg("quiet_start_hour").toInt());
    if (server.hasArg("quiet_start_minute")) configManager.setQuietStartMinute(server.arg("quiet_start_minute").toInt());
    if (server.hasArg("quiet_end_hour")) configManager.setQuietEndHour(server.arg("quiet_end_hour").toInt());
    if (server.hasArg("quiet_end_minute")) configManager.setQuietEndMinute(server.arg("quiet_end_minute").toInt());
    
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
    // §5.5: мягкий старт — НЕ блокируем ядро delay()'ем. Тяжёлая периферия (LED/зуммер/WiFi)
    //       включается по истечении SOFT_START_DELAY_MS через millis() в loop().
    //       Это даёт boost-конвертеру UPS время стабилизироваться без стартового пика тока.
    Serial.begin(115200);
    Serial.println("\n\n====================================");
    Serial.println("🚀 ESS Watcher: СИСТЕМА ЗАПУЩЕНА");
    Serial.println("====================================\n");

    // §4.1: полностью выключаем и освобождаем BT/BLE (цель №4 — никакого Bluetooth).
    //       Делаем в самом начале, до поднятия любой периферии.
    btStop();
    esp_bt_controller_disable();
    esp_bt_mem_release(ESP_BT_MODE_BTDM);
    Serial.println("🔇 BT/BLE отключён, память освобождена.");

    // §6.3: стартуем на экономной частоте 80 МГц. До 160 МГц поднимаемся только на время SSL.
    setCpuFrequencyMhz(80);

    configManager.begin();
    setenv("TZ", configManager.getTimezone(), 1);
    tzset();
    SoundStorage::begin();
    SoundStorage::listSounds();

    pinMode(PIN_POWER_SENSE, INPUT_PULLDOWN);
    pinMode(PIN_CONFIG_BUTTON, INPUT_PULLUP);

    // §5.5/§5.6: читаем статус сети СРАЗУ (до WiFi, до тяжёлой периферии).
    //   Это безопасно: цифровой пин не нагружает UPS.
    isPowerOn = digitalRead(PIN_POWER_SENSE) == HIGH;
    lastPowerState = isPowerOn;
    potentialPowerState = isPowerOn;

    secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

    bootTime = millis();
    softStartComplete = false;
    wifiStarted = false;
    Serial.println("⏳ Мягкий старт: тяжёлая периферия будет включена через 2 с.");
}

void loop() {
    // §5.5: мягкий старт — тяжёлая периферия (LED/зуммер/WiFi) включается через 2 с.
    //       До этого момента loop() крутится лёгким циклом, CPU в IDLE на 80 МГц.
    if (!softStartComplete) {
        if (millis() - bootTime >= SOFT_START_DELAY_MS) {
            softStartComplete = true;
            Serial.println("✅ Мягкий старт завершён. Включаю периферию.");

            FastLED.addLeds<WS2812B, PIN_LED_DATA, GRB>(leds, NUM_LEDS);
            buzzer.begin(PIN_BUZZER, 0);
            buzzer.setVolume(configManager.getBuzzerVolume());

            // Звук включения устройства (только при настоящем старте, не brownout, не в тихий час)
            if (!isRebootAfterBrownout() && !isQuietHours()) {
                String startupSound = SoundStorage::getSystemSound("startup");
                if (startupSound.length() > 0) {
                    buzzer.play(startupSound);
                } else {
                    buzzer.play("startup:d=4,o=6,b=300:4b");
                }
            } else {
                if (isRebootAfterBrownout()) {
                    Serial.println("⏭️ Brownout-ресет: startup-звук пропускаю.");
                } else {
                    Serial.println("🔇 Тихий режим: startup-звук пропускаю.");
                }
            }
        } else {
            return; // ещё рано — крутимся лёгким циклом
        }
    }

    // §3.1/§5.6: lazy WiFi — поднимаем спустя WIFI_STARTUP_DELAY_MS после soft-start,
    //            НЕ при включении устройства (цель №3).
    if (softStartComplete && !wifiStarted) {
        if (millis() - bootTime >= SOFT_START_DELAY_MS + WIFI_STARTUP_DELAY_MS) {
            wifiStarted = true;
            Serial.println("🌐 Поднимаю WiFi (lazy-init)...");
            WiFi.onEvent(WiFiEvent);
            WiFi.mode(WIFI_STA);
            WiFi.setTxPower(WIFI_POWER_5dBm);   // §2.1: ограничиваем TX-мощность (анти-скачок тока)
            WiFi.setAutoReconnect(true);
            WiFi.begin(configManager.getSSID(), configManager.getPass());
        }
    }

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

    // §2.1: отправка отложенного сообщения — ТОЛЬКО после завершения мелодии.
    //       Пока играет мелодия, не дёргаем WiFi/TLS — это исключает коллизию токов.
    //       Для стартового сообщения awaitMelody=false (мелодии нет параллельно).
    if (pendingMessage.length() > 0) {
        bool canSend = !pendingMessageAwaitMelody || !buzzer.playing();
        if (canSend) {
            String msg = pendingMessage;
            pendingMessage = "";
            pendingMessageAwaitMelody = false;
            sendTelegramMessage(msg);
        }
    }

    // 3. Мониторинг питания
    if (millis() - lastPowerCheckTime >= POWER_CHECK_INTERVAL) {
        lastPowerCheckTime = millis();
        bool currentReading = digitalRead(PIN_POWER_SENSE) == HIGH;

        if (currentReading == potentialPowerState) debounceCounter++;
        else { debounceCounter = 0; potentialPowerState = currentReading; }

        if (debounceCounter >= DEBOUNCE_THRESHOLD) {
            if (potentialPowerState != lastPowerState) {
                lastPowerState = potentialPowerState;
                isPowerOn = potentialPowerState;

                // §5.6: сохраняем новое состояние в NVS (для распознавания brownout при следующем старте)
                configManager.setLastKnownPowerState(isPowerOn);
                configManager.save();

                // §2.3: гасим LED на 3 с — не добавляем пиковый ток в момент события
                ledBlackoutUntil = millis() + LED_BLACKOUT_AFTER_EVENT_MS;

                // §2.1: СНАЧАЛА мелодия, ПОТОМ сообщение (после завершения мелодии).
                //       pendingMessage уйдёт в Telegram только когда buzzer.playing() == false.
                if (isPowerOn) {
                    Serial.println("⚡ Питание: СЕТЬ 220В ВОССТАНОВЛЕНА");
                    if (!isQuietHours()) {
                        playMelody(configManager.getMelodyOnFile(), true);
                        queueMessage(configManager.getMsgPowerRestored(), /*awaitMelody=*/true);
                    } else {
                        queueMessage(configManager.getMsgPowerRestored(), /*awaitMelody=*/false);
                    }
                } else {
                    Serial.println("🚨 Питание: ОТКЛЮЧЕНИЕ 220В!");
                    if (!isQuietHours()) {
                        playMelody(configManager.getMelodyOffFile(), false);
                        queueMessage(configManager.getMsgPowerLost(), /*awaitMelody=*/true);
                    } else {
                        queueMessage(configManager.getMsgPowerLost(), /*awaitMelody=*/false);
                    }
                }
            }
        }
    }
    delay(10);
}
