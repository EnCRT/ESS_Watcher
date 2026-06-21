#pragma once

const char WEB_PORTAL_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESS Watcher - Настройка</title>
    <style>
        :root {
            --primary: #6366f1;
            --green: #10b981;
            --red: #ef4444;
            --bg-start: #f8fafc;
            --bg-end: #e2e8f0;
        }

        body {
            font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
            margin: 0;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            background: linear-gradient(-45deg, #f8fafc, #f1f5f9, #e2e8f0, #f8fafc);
            background-size: 400% 400%;
            animation: gradient 15s ease infinite;
            color: #1e293b;
        }

        @keyframes gradient {
            0% { background-position: 0% 50%; }
            50% { background-position: 100% 50%; }
            100% { background-position: 0% 50%; }
        }

        .container {
            background: rgba(255, 255, 255, 0.8);
            backdrop-filter: blur(10px);
            padding: 2.5rem;
            border-radius: 24px;
            box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.1), 0 8px 10px -6px rgba(0, 0, 0, 0.1);
            width: 100%;
            max-width: 400px;
            text-align: center;
        }

        h1 { font-size: 1.5rem; margin-bottom: 0.5rem; color: #0f172a; }
        p { color: #64748b; margin-bottom: 2rem; font-size: 0.9rem; }

        .field { margin-bottom: 1.2rem; text-align: left; }
        label { display: block; font-size: 0.8rem; font-weight: 600; margin-bottom: 0.4rem; color: #475569; }

        .section-title {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            font-size: 0.85rem;
            font-weight: 700;
            color: #334155;
            margin-bottom: 0.6rem;
            padding-bottom: 0.4rem;
            border-bottom: 1px solid #e2e8f0;
        }

        .section-title .icon {
            font-size: 1.1rem;
        }
        
        .radio-group {
            display: flex;
            flex-direction: column;
            gap: 0.5rem;
            background: #f1f5f9;
            padding: 0.8rem;
            border-radius: 12px;
            margin-top: 0.5rem;
        }

        .radio-group.on-group {
            border-left: 3px solid var(--green);
        }

        .radio-group.off-group {
            border-left: 3px solid var(--red);
        }

        .radio-label {
            display: flex;
            align-items: center;
            gap: 0.8rem;
            cursor: pointer;
            font-size: 0.9rem;
            color: #475569;
            font-weight: 500;
            padding: 0.3rem 0;
            transition: color 0.2s;
        }

        .radio-label:hover { color: var(--primary); }

        .radio-label input[type="radio"] {
            appearance: none;
            width: 18px;
            height: 18px;
            border: 2px solid #cbd5e1;
            border-radius: 50%;
            outline: none;
            transition: all 0.2s;
            position: relative;
            flex-shrink: 0;
        }

        .radio-label input[type="radio"]:checked {
            border-color: var(--primary);
            background: var(--primary);
        }

        .radio-label input[type="radio"]:checked::after {
            content: '';
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            width: 6px;
            height: 6px;
            background: white;
            border-radius: 50%;
        }
        
        input[type="text"],
        input[type="password"] {
            width: 100%;
            padding: 0.8rem;
            border: 1px solid #e2e8f0;
            border-radius: 12px;
            font-size: 1rem;
            transition: all 0.2s;
            box-sizing: border-box;
            background: white;
        }

        .melody-row {
            display: flex;
            gap: 0.5rem;
            align-items: center;
        }

        select.melody-select {
            flex: 1;
            padding: 0.8rem;
            border: 1px solid #e2e8f0;
            border-radius: 12px;
            font-size: 1rem;
            background: white;
            box-sizing: border-box;
            cursor: pointer;
        }

        .play-btn {
            width: 44px;
            height: 44px;
            min-width: 44px;
            border-radius: 50%;
            border: none;
            background: var(--primary);
            color: white;
            font-size: 1.2rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: all 0.2s;
            margin: 0;
            padding: 0;
        }

        .play-btn:hover { transform: scale(1.1); }
        .play-btn:active { transform: scale(0.95); }
        .play-btn.playing { background: #f59e0b; }

        .volume-row {
            display: flex;
            gap: 0.5rem;
            margin-bottom: 0.5rem;
        }

        .vol-btn {
            flex: 1;
            padding: 0.6rem 0;
            border: 2px solid #e2e8f0;
            border-radius: 10px;
            background: white;
            color: #475569;
            font-size: 0.85rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
            margin: 0;
        }

        .vol-btn:hover { border-color: var(--primary); color: var(--primary); }
        .vol-btn.active { background: var(--primary); color: white; border-color: var(--primary); }

        .vol-mute-btn {
            width: 100%;
            padding: 0.6rem;
            border: 2px solid #e2e8f0;
            border-radius: 10px;
            background: white;
            color: #475569;
            font-size: 0.85rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
            margin: 0 0 1rem 0;
        }

        .vol-mute-btn:hover { border-color: #ef4444; color: #ef4444; }
        .vol-mute-btn.muted { background: #ef4444; color: white; border-color: #ef4444; }

        textarea.msg-input {
            width: 100%;
            padding: 0.6rem;
            border: 1px solid #e2e8f0;
            border-radius: 10px;
            font-size: 0.85rem;
            font-family: inherit;
            resize: vertical;
            box-sizing: border-box;
            background: white;
        }

        textarea.msg-input:focus {
            outline: none;
            border-color: var(--primary);
            box-shadow: 0 0 0 3px rgba(99, 102, 241, 0.1);
        }

        .msg-label {
            font-size: 0.75rem;
            font-weight: 600;
            color: #64748b;
            margin-bottom: 0.3rem;
            display: flex;
            align-items: center;
            gap: 0.3rem;
        }

        input[type="text"]:focus,
        input[type="password"]:focus {
            outline: none;
            border-color: var(--primary);
            box-shadow: 0 0 0 4px rgba(99, 102, 241, 0.1);
        }

        .divider {
            height: 1px;
            background: #e2e8f0;
            margin: 1.5rem 0;
        }

        button {
            width: 100%;
            padding: 1rem;
            background: var(--primary);
            color: white;
            border: none;
            border-radius: 12px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s;
            margin-top: 1rem;
            position: relative;
            overflow: hidden;
        }

        button:hover { transform: translateY(-1px); box-shadow: 0 4px 12px rgba(99, 102, 241, 0.3); }
        button:active { transform: translateY(0); }

        button.success {
            background: #10b981;
        }

        .quiet-toggle {
            position: relative;
            display: inline-block;
            width: 48px;
            height: 26px;
        }

        .quiet-toggle input { opacity: 0; width: 0; height: 0; }

        .quiet-slider {
            position: absolute;
            cursor: pointer;
            top: 0; left: 0; right: 0; bottom: 0;
            background-color: #cbd5e1;
            transition: 0.3s;
            border-radius: 26px;
        }

        .quiet-slider:before {
            position: absolute;
            content: "";
            height: 20px;
            width: 20px;
            left: 3px;
            bottom: 3px;
            background-color: white;
            transition: 0.3s;
            border-radius: 50%;
        }

        input:checked + .quiet-slider { background-color: var(--green); }
        input:checked + .quiet-slider:before { transform: translateX(22px); }

        .quiet-row {
            display: flex;
            align-items: center;
            gap: 0.6rem;
            flex-wrap: wrap;
        }

        .quiet-label {
            font-size: 0.8rem;
            font-weight: 600;
            color: #475569;
            white-space: nowrap;
        }

        .quiet-time-row {
            display: flex;
            align-items: center;
            gap: 0.3rem;
        }

        .quiet-time-row input[type="number"] {
            width: 52px;
            padding: 0.5rem;
            border: 1px solid #e2e8f0;
            border-radius: 8px;
            font-size: 0.9rem;
            text-align: center;
            box-sizing: border-box;
            background: white;
        }

        .quiet-time-row span {
            font-weight: 600;
            color: #64748b;
        }

        .quiet-time-row input[type="number"]:focus {
            outline: none;
            border-color: var(--primary);
            box-shadow: 0 0 0 3px rgba(99, 102, 241, 0.1);
        }

        button.success::after {
            content: ' ✓ Сохранено';
        }

        button.loading { opacity: 0.7; cursor: not-allowed; }

        .footer {
            margin-top: 2rem;
            font-size: 0.8rem;
            color: #94a3b8;
        }

        .footer a {
            color: var(--primary);
            text-decoration: none;
            font-weight: 600;
        }

        .footer a:hover {
            text-decoration: underline;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESS Watcher</h1>
        <p>Настройка подключения и Telegram</p>
        
        <form id="configForm">
            <div class="field">
                <label>Wi-Fi SSID</label>
                <input type="text" name="ssid" value="{{ssid}}" required>
            </div>
            <div class="field">
                <label>Wi-Fi Password</label>
                <input type="password" name="pass" value="{{pass}}" required>
            </div>
            <div class="field">
                <label>Telegram Bot Token</label>
                <input type="text" name="token" value="{{token}}" required>
            </div>
            <div class="field">
                <label>Chat ID</label>
                <input type="text" name="chat" value="{{chat}}" required>
            </div>
            <div class="field">
                <label>Thread ID (опционально)</label>
                <input type="text" name="thread" value="{{thread}}">
            </div>

            <div class="divider"></div>

            <div class="field">
                <div class="section-title">
                    <span class="icon">⚡</span>
                    <span>Мелодия: Сеть 220В восстановлена</span>
                </div>
                <div class="melody-row">
                    <select name="melody_on" class="melody-select" id="melody_on">
                        {{melody_on_options}}
                    </select>
                    <button type="button" class="play-btn" onclick="playMelody('melody_on')" title="Прослушать">▶</button>
                </div>
            </div>

            <div class="field">
                <div class="section-title">
                    <span class="icon">🚨</span>
                    <span>Мелодия: Отключение 220В</span>
                </div>
                <div class="melody-row">
                    <select name="melody_off" class="melody-select" id="melody_off">
                        {{melody_off_options}}
                    </select>
                    <button type="button" class="play-btn" onclick="playMelody('melody_off')" title="Прослушать">▶</button>
                </div>
            </div>

            <div class="divider"></div>

            <div class="field">
                <div class="section-title">
                    <span class="icon">🔊</span>
                    <span>Громкость бузера</span>
                </div>
                <div class="volume-row">
                    <button type="button" class="vol-btn" data-vol="25" onclick="setVolume(25)">25%</button>
                    <button type="button" class="vol-btn" data-vol="50" onclick="setVolume(50)">50%</button>
                    <button type="button" class="vol-btn" data-vol="75" onclick="setVolume(75)">75%</button>
                    <button type="button" class="vol-btn" data-vol="100" onclick="setVolume(100)">100%</button>
                </div>
                <button type="button" class="vol-mute-btn" data-vol="0" onclick="setMute()" id="muteBtn">🔇 Без звука</button>
            </div>

            <div class="divider"></div>

            <div class="field">
                <div class="section-title">
                    <span class="icon">🌙</span>
                    <span>Тихий режим</span>
                </div>
                <div class="quiet-row" style="margin-bottom:0.6rem">
                    <span class="quiet-label">Включить</span>
                    <label class="quiet-toggle">
                        <input type="checkbox" name="quiet_enabled" id="quiet_enabled" {{quiet_checked}}>
                        <span class="quiet-slider"></span>
                    </label>
                </div>
                <div class="quiet-row" style="margin-bottom:0.4rem">
                    <span class="quiet-label">С</span>
                    <div class="quiet-time-row">
                        <input type="number" name="quiet_start_hour" min="0" max="23" value="{{quiet_start_hour}}">
                        <span>:</span>
                        <input type="number" name="quiet_start_minute" min="0" max="59" value="{{quiet_start_minute}}">
                    </div>
                    <span class="quiet-label">до</span>
                    <div class="quiet-time-row">
                        <input type="number" name="quiet_end_hour" min="0" max="23" value="{{quiet_end_hour}}">
                        <span>:</span>
                        <input type="number" name="quiet_end_minute" min="0" max="59" value="{{quiet_end_minute}}">
                    </div>
                </div>
            </div>

            <div class="divider"></div>

            <div class="field">
                <div class="section-title">
                    <span class="icon">💬</span>
                    <span>Тексты Telegram-сообщений</span>
                </div>
                <span class="msg-label">🚀 Запуск (сеть есть)</span>
                <textarea name="msg_startup_power" class="msg-input" rows="2">{{msg_startup_power}}</textarea>
                
                <span class="msg-label">⚡️ Запуск (от АКБ)</span>
                <textarea name="msg_startup_battery" class="msg-input" rows="2">{{msg_startup_battery}}</textarea>
                
                <span class="msg-label">♻️ Сеть восстановлена</span>
                <textarea name="msg_power_restored" class="msg-input" rows="2">{{msg_power_restored}}</textarea>
                
                <span class="msg-label">🚨 Отключение сети</span>
                <textarea name="msg_power_lost" class="msg-input" rows="2">{{msg_power_lost}}</textarea>
            </div>
            
            <button type="submit" id="saveBtn">Сохранить</button>
        </form>
        <div class="footer">
            Разработка: <a href="https://t.me/encrt" target="_blank">@encrt</a>
        </div>
    </div>

    <script>
        let lastVolume = parseInt('{{volume}}') || 100;

        (function() {
            const vol = lastVolume;
            if (vol === 0) {
                document.getElementById('muteBtn').classList.add('muted');
                document.getElementById('muteBtn').textContent = '🔇 Звук выключен';
            } else {
                document.querySelector(`.vol-btn[data-vol="${vol}"]`)?.classList.add('active');
            }
        })();

        async function setVolume(vol) {
            lastVolume = vol;
            document.querySelectorAll('.vol-btn').forEach(b => b.classList.remove('active'));
            document.querySelector(`.vol-btn[data-vol="${vol}"]`)?.classList.add('active');
            document.getElementById('muteBtn').classList.remove('muted');
            document.getElementById('muteBtn').textContent = '🔇 Без звука';
            document.getElementById('melody_on').focus();
            await fetch('/volume?vol=' + vol);
        }

        async function setMute() {
            const btn = document.getElementById('muteBtn');
            if (btn.classList.contains('muted')) {
                setVolume(lastVolume);
                return;
            }
            document.querySelectorAll('.vol-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('muted');
            btn.textContent = '🔇 Звук выключен';
            document.getElementById('melody_on').focus();
            await fetch('/volume?vol=0');
        }

        async function playMelody(selectId) {
            const select = document.getElementById(selectId);
            const btn = select.parentElement.querySelector('.play-btn');
            const file = select.value;
            if (btn.classList.contains('playing')) return;

            btn.classList.add('playing');
            btn.textContent = '⏳';
            select.focus();
            try {
                await fetch('/play?file=' + encodeURIComponent(file));
            } catch(e) {}
            setTimeout(() => {
                btn.classList.remove('playing');
                btn.textContent = '▶';
            }, 1500);
        }

        document.getElementById('configForm').onsubmit = async (e) => {
            e.preventDefault();
            const btn = document.getElementById('saveBtn');
            const originalText = btn.innerText;
            
            btn.classList.add('loading');
            btn.innerText = 'Сохранение...';
            
            const formData = new FormData(e.target);
            const params = new URLSearchParams(formData);
            
            try {
                const response = await fetch('/save', {
                    method: 'POST',
                    body: params
                });
                
                if (response.ok) {
                    btn.innerText = '';
                    btn.classList.remove('loading');
                    btn.classList.add('success');
                    setTimeout(() => {
                        window.location.reload();
                    }, 3000);
                } else {
                    throw new Error();
                }
            } catch (err) {
                alert('Ошибка при сохранении!');
                btn.classList.remove('loading');
                btn.innerText = originalText;
            }
        };

    </script>
</body>
</html>
)=====";
