#include <Arduino.h>

// На SuperMini встроенный синий светодиод на 8 пине
#define LED_PIN 8

void setup() {
    // Настройка пина на выход
    pinMode(LED_PIN, OUTPUT);
    // Запуск Serial для отладки
    Serial.begin(115200);
}

void loop() {
    Serial.println("LED ON");
    digitalWrite(LED_PIN, LOW);  // Включить (инвертировано)
    delay(1000);

    Serial.println("LED OFF");
    digitalWrite(LED_PIN, HIGH); // Выключить (инвертировано)
    delay(1000);
}
