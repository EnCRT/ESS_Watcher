#pragma once

#include <Arduino.h>

// =====================================================================================
// =               RTTTL-парсер и асинхронный проигрыватель мелодий                     =
// =               для пассивного зуммера KY-006 через LEDC PWM                        =
// =====================================================================================

class BuzzerManager {
private:
    int pin;
    int ledcChannel;
    uint8_t volume; // 0-100, где 100 = максимальная громкость

    // RTTTL parsing state
    String currentMelody;
    int parsePos;
    int defaultDuration;
    int defaultOctave;
    int bpm;
    long wholeNote; // длительность целой ноты в мс

    // Async playback state
    bool isPlaying;
    unsigned long noteStartTime;
    unsigned long currentNoteDuration;
    bool noteIsRest; // текущий шаг — пауза (после ноты)
    unsigned long restDuration;

    // Таблица частот нот (октава 4)
    // c, c#, d, d#, e, f, f#, g, g#, a, a#, b
    static constexpr int NOTE_FREQS[] = {
        262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
    };

    void playTone(int freq) {
        if (freq == 0) {
            ledcWrite(ledcChannel, 0);
        } else {
            uint32_t duty = (255UL * volume) / 200; // 100% vol = 50% duty (max for square wave)
            ledcSetup(ledcChannel, freq, 8);
            ledcWrite(ledcChannel, duty);
        }
    }

    int getNoteIndex(char note, bool sharp) {
        int idx = -1;
        switch (note) {
            case 'c': idx = 0; break;
            case 'd': idx = 2; break;
            case 'e': idx = 4; break;
            case 'f': idx = 5; break;
            case 'g': idx = 7; break;
            case 'a': idx = 9; break;
            case 'b': idx = 11; break;
        }
        if (sharp && idx >= 0) idx++;
        return idx;
    }

    // Пропуск пробелов и запятых
    void skipWhitespace() {
        while (parsePos < (int)currentMelody.length()) {
            char c = currentMelody[parsePos];
            if (c == ' ' || c == ',') parsePos++;
            else break;
        }
    }

    // Парсим заголовок RTTTL: "name:d=4,o=5,b=140:"
    void parseHeader() {
        // Пропуск имени мелодии (до первого ':')
        int colonPos = currentMelody.indexOf(':');
        if (colonPos < 0) { isPlaying = false; return; }
        parsePos = colonPos + 1;

        // Парсинг значений по умолчанию
        defaultDuration = 4;
        defaultOctave = 6;
        bpm = 63;

        int secondColon = currentMelody.indexOf(':', parsePos);
        if (secondColon < 0) { isPlaying = false; return; }

        String defaults = currentMelody.substring(parsePos, secondColon);
        defaults.toLowerCase();
        defaults.trim();

        // Разбираем "d=4,o=6,b=63"
        int pos = 0;
        while (pos < (int)defaults.length()) {
            char key = defaults[pos];
            // Ищем '='
            int eqPos = defaults.indexOf('=', pos);
            if (eqPos < 0) break;
            int commaPos = defaults.indexOf(',', eqPos);
            if (commaPos < 0) commaPos = defaults.length();

            String val = defaults.substring(eqPos + 1, commaPos);
            val.trim();

            switch (key) {
                case 'd': defaultDuration = val.toInt(); break;
                case 'o': defaultOctave = val.toInt(); break;
                case 'b': bpm = val.toInt(); break;
            }

            pos = commaPos + 1;
            // Пропуск пробелов
            while (pos < (int)defaults.length() && defaults[pos] == ' ') pos++;
        }

        if (defaultDuration <= 0) defaultDuration = 4;
        if (defaultOctave < 4 || defaultOctave > 7) defaultOctave = 6;
        if (bpm <= 0) bpm = 63;

        wholeNote = (60000L * 4L) / bpm;
        parsePos = secondColon + 1;
    }

    // Парсим и воспроизводим следующую ноту. Возвращает false если мелодия закончилась.
    bool parseAndPlayNextNote() {
        skipWhitespace();
        if (parsePos >= (int)currentMelody.length()) return false;

        // 1. Длительность ноты (опционально)
        int duration = 0;
        while (parsePos < (int)currentMelody.length() && isDigit(currentMelody[parsePos])) {
            duration = duration * 10 + (currentMelody[parsePos] - '0');
            parsePos++;
        }
        if (duration == 0) duration = defaultDuration;

        if (parsePos >= (int)currentMelody.length()) return false;

        // 2. Название ноты
        char note = tolower(currentMelody[parsePos]);
        parsePos++;

        // 3. Диез (#) ?
        bool sharp = false;
        if (parsePos < (int)currentMelody.length() && currentMelody[parsePos] == '#') {
            sharp = true;
            parsePos++;
        }

        // 4. Точка (увеличивает длительность на 50%) ?
        bool dotted = false;
        if (parsePos < (int)currentMelody.length() && currentMelody[parsePos] == '.') {
            dotted = true;
            parsePos++;
        }

        // 5. Октава (опционально)
        int octave = defaultOctave;
        if (parsePos < (int)currentMelody.length() && isDigit(currentMelody[parsePos])) {
            octave = currentMelody[parsePos] - '0';
            parsePos++;
        }

        // Еще одна проверка на точку (может быть после октавы)
        if (parsePos < (int)currentMelody.length() && currentMelody[parsePos] == '.') {
            dotted = true;
            parsePos++;
        }

        // Вычисляем длительность в мс
        unsigned long noteDur = wholeNote / duration;
        if (dotted) noteDur = noteDur + noteDur / 2;

        // Пауза (p) или нота?
        if (note == 'p') {
            playTone(0);
            currentNoteDuration = noteDur;
            noteIsRest = false;
        } else {
            int noteIdx = getNoteIndex(note, sharp);
            if (noteIdx < 0) {
                playTone(0);
                currentNoteDuration = noteDur;
                noteIsRest = false;
            } else {
                int freq = NOTE_FREQS[noteIdx];
                for (int i = 4; i < octave; i++) freq *= 2;
                for (int i = 4; i > octave; i--) freq /= 2;

                unsigned long playTime = noteDur * 9 / 10;
                restDuration = noteDur - playTime;

                playTone(freq);
                currentNoteDuration = playTime;
                noteIsRest = false;
            }
        }

        noteStartTime = millis();
        return true;
    }

public:
    BuzzerManager() : pin(-1), ledcChannel(0), volume(100), isPlaying(false), parsePos(0),
                      defaultDuration(4), defaultOctave(6), bpm(63), wholeNote(0),
                      noteStartTime(0), currentNoteDuration(0), noteIsRest(false),
                      restDuration(0) {}

    void begin(int buzzerPin, int channel = 0) {
        pin = buzzerPin;
        ledcChannel = channel;
        ledcSetup(ledcChannel, 2000, 8);
        ledcAttachPin(pin, ledcChannel);
        stop();
    }

    // Запустить воспроизведение RTTTL-строки (неблокирующее)
    void play(const String& rtttl) {
        stop();
        currentMelody = rtttl;
        currentMelody.trim();
        if (currentMelody.length() == 0) return;

        parsePos = 0;
        isPlaying = true;
        parseHeader();

        if (isPlaying) {
            if (!parseAndPlayNextNote()) {
                stop();
            }
        }
    }

    // Простой одиночный тональный сигнал
    void beep(int freq, unsigned long duration) {
        stop();
        currentMelody = "";
        isPlaying = true;
        noteIsRest = false;
        currentNoteDuration = duration;
        restDuration = 0;
        noteStartTime = millis();
        playTone(freq);
    }

    void stop() {
        isPlaying = false;
        playTone(0);
        currentMelody = "";
        parsePos = 0;
    }

    bool playing() const { return isPlaying; }

    void setVolume(uint8_t vol) { volume = constrain(vol, 0, 100); }
    uint8_t getVolume() const { return volume; }

    // Вызывать из loop() — неблокирующий обработчик
    void handleAsync() {
        if (!isPlaying) return;

        unsigned long now = millis();

        if (!noteIsRest) {
            if (now - noteStartTime >= currentNoteDuration) {
                if (restDuration > 0) {
                    playTone(0);
                    noteIsRest = true;
                    noteStartTime = now;
                    currentNoteDuration = restDuration;
                    restDuration = 0;
                } else {
                    // Без паузы — следующая нота или стоп
                    if (currentMelody.length() == 0 || !parseAndPlayNextNote()) {
                        stop();
                    }
                }
            }
        } else {
            // Сейчас межнотная пауза — ждем конца
            if (now - noteStartTime >= currentNoteDuration) {
                noteIsRest = false;
                if (!parseAndPlayNextNote()) {
                    stop();
                }
            }
        }
    }
};

// Статическая инициализация массива частот
constexpr int BuzzerManager::NOTE_FREQS[];
