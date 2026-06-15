#pragma once

#include <Arduino.h>
#include <LittleFS.h>

// =====================================================================================
// =       Загрузка RTTTL-мелодий из файловой системы LittleFS (папка /sounds)          =
// =====================================================================================

class SoundStorage {
public:
    static bool begin() {
        if (!LittleFS.begin(true)) {
            Serial.println("❌ SoundStorage: Не удалось смонтировать LittleFS!");
            return false;
        }
        Serial.println("✅ SoundStorage: LittleFS смонтирован.");
        return true;
    }

    // Получить RTTTL-строку по ключу из файла /sounds/system.txt
    // Ключ — например "startup" или "wifi_mode"
    static String getSystemSound(const String& key) {
        return getSoundFromFile("/sounds/system.txt", key);
    }

    // Получить RTTTL-строку по ключу из произвольного файла
    static String getSoundFromFile(const String& filePath, const String& key) {
        File file = LittleFS.open(filePath, "r");
        if (!file) {
            Serial.print("⚠️ SoundStorage: Файл не найден: ");
            Serial.println(filePath);
            return "";
        }

        String result = "";
        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();

            // Пропуск пустых строк и комментариев
            if (line.length() == 0 || line.startsWith("#")) continue;

            // Формат: key=RTTTL-строка
            int eqPos = line.indexOf('=');
            if (eqPos < 0) continue;

            String lineKey = line.substring(0, eqPos);
            lineKey.trim();

            if (lineKey == key) {
                result = line.substring(eqPos + 1);
                result.trim();
                break;
            }
        }

        file.close();

        if (result.length() == 0) {
            Serial.print("⚠️ SoundStorage: Ключ не найден: ");
            Serial.println(key);
        }

        return result;
    }

    // Собрать HTML-строку <option> для выпадающего списка мелодий в веб-форме
    static String buildMelodyOptions(const String& selected) {
        String opts = "<option value=\"\">— Стандартная (встроенная) —</option>";
        File root = LittleFS.open("/sounds");
        if (!root || !root.isDirectory()) {
            Serial.println("⚠️ SoundStorage: /sounds не является директорией. Залейте файлы: pio run -t uploadfs");
            return opts;
        }
        File file = root.openNextFile();
        while (file) {
            String filePath = String(file.path());
            String fileName = filePath.startsWith("/sounds/") ? filePath.substring(8) : filePath;
            if (filePath.endsWith(".txt") && filePath != "/sounds/system.txt" && fileName != "system.txt") {
                opts += "<option value=\"" + fileName + "\"";
                if (fileName == selected) opts += " selected";
                opts += ">" + fileName + "</option>";
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
        return opts;
    }

    // Вывести список файлов в /sounds/ (для отладки)
    static void listSounds() {
        File root = LittleFS.open("/sounds");
        if (!root || !root.isDirectory()) {
            Serial.println("⚠️ SoundStorage: Папка /sounds не найдена. Залейте файлы: pio run -t uploadfs");
            return;
        }

        Serial.println("📂 SoundStorage: Содержимое /sounds/:");
        File file = root.openNextFile();
        while (file) {
            Serial.print("   📄 ");
            Serial.print(file.path());
            Serial.print(" (");
            Serial.print(file.size());
            Serial.println(" байт)");
            file.close();
            file = root.openNextFile();
        }
        root.close();
    }
};
