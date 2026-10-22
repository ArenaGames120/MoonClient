# MoonClient

Клиент-мод для **DDraceNetwork (DDNet) 20.2** — форк официального клиента с дополнительными функциями. Работает с обычными DDNet-серверами и с собственным `MoonServer` (собирается вместе с клиентом).

> Форк открытого DDNet (лицензия zlib, `license.txt`). Всё честно: исходники здесь же, модификации лежат в `src/game/client/components/` и `/src/game/` рядом с обычными компонентами.

## Возможности

| Фича | Что делает |
|---|---|
| **Голосовой ИИ-ассистент** | Говоришь в микрофон → локальная нейросеть Whisper (офлайн, без ключей) распознаёт речь → бесплатный открытый LLM отвечает → Google TTS озвучивает ответ голосом. В чате видно `[Ты сказал]` и `[ИИ]`, есть мини-память диалога. |
| **ИИ-перевод чата** | Переводит чужие сообщения в чате через Google Translate на выбранный язык (кэш, очередь, не переводит собственные сообщения и коды `/load`). |
| **ASMR-эмбиент** | Свой эмбиент (дождь/ветер/волны) из `data/moon/asmr/*` с регулировкой громкости, крутится по кругу. |
| **Хук-доводка прицела** | Доводит прицел на цель в зоне FOV при нажатии клавиши крюка (учтите: на публичных DDNet-серверах такие подсказки прицела могут считаться читами — используйте на своих серверах). |
| | Плюс: ассистент сам выбирает первый рабочий микрофон (виртуальные микрофоны типа Voicemod перескакивает), диагностику пишет в чат. |

## Голосовой ассистент — как пользоваться

1. Первое нажатие привязанной клавиши — начинаем слушать (до ~12 секунд).
2. Пауза ~0.9 секунды после речи — запись заканчивается сама; повторное нажатие тоже останавливает.
3. Смотри распознавание и ответ в чате, ответ звучит голосом.

Привязка клавиши (консоль, F2):

```
bind T moon_voice
```

Дополнительные команды/переменные:

```
moon_voice_stop        — отменить текущую запись
mc_voice_lang ru       — язык распознавания и озвучки (ru, en, de, ...)
mc_voice_asst 1/0      — включить/выключить ассистента (галочка есть в настройках)
mc_voice_volume 80     — громкость голосового ответа (ползунок в настройках)
```

## Модель Whisper

Распознавание идёт локально, модель положи сюда:

```
data/moon/voice/model.bin
```

Скачай `ggml-base-q5_1.bin` (~59 МБ, квантованная base, мультиязычная) с HuggingFace:
<https://huggingface.co/ggerganov/whisper.cpp> → файл `ggml-base-q5_1.bin` → переименуй в `model.bin`.

Без модели остальное работает, ассистент скажет «Файл модели ИИ не найден».

## Сборка (Windows, MSYS2 UCRT64)

1. Установи [MSYS2](https://www.msys2.org/) и в **UCRT64 shell**:

```bash
pacman -Syu
pacman -S --needed make mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,rust,SDL2,curl,sqlite3,freetype,libpng,opus,opusfile,libogg,wavpack}
```

(Rust нужен как Windows-тулчейн: `rustup` с `cargo` в PATH тоже подходит.)

2. Собери:

```bash
cd MoonClient
cmake . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" -DCMAKE_C_FLAGS_RELEASE="-O1 -DNDEBUG" \
  -DCLIENT=ON -DSERVER=ON -DSERVER_EXECUTABLE=MoonServer \
  -DTOOLS=OFF -DDISCORD=OFF -DWEBSOCKETS=OFF -DMYSQL=OFF -DUPNP=ON \
  -DVIDEORECORDER=OFF -DPRECOMPILE_HEADERS=OFF -DPREFER_BUNDLED_LIBS=OFF \
  -DAUTOUPDATE=OFF -DTESTS=OFF -DDOWNLOAD_GTEST=OFF -DSECURITY_COMPILER_FLAGS=OFF
ninja -C build DDNet.exe MoonServer.exe
```

Все библиотеки берутся из MSYS2 (папка `ddnet-libs` не нужна, она пустая). Режим `-O1` — чтобы не падал компилятор на слабых машинах; если хочешь быстрее основы — подними до `-O2/-O3`.

3. Положи `build/DDNet.exe` рядом с папкой `data/` из репозитория (или собери дистрибутив сам: exe + `data/` + DLL из `ucrt64/bin` по зависимостям — проверь через `ldd`).

Каталоги:

```
data/moon/            — файлы мода (не изменяй имена)
├── asmr/             — эмбиент-треки (.opus) + README
└── voice/            — сюда кладётся model.bin (Whisper)
src/game/client/components/
├── moonvoice.*       — голосовой ассистент
├── chat.cpp/.h       — ИИ-перевод чата (интегрировано в оригинальный чат)
├── sounds.cpp/.h     — ASMR-пайплайн
└── menus_settings_moonclient.cpp — страница настроек MoonClient
src/engine/external/
├── whisper/          — вендорено whisper.cpp (MIT)
└── minimp3/          — вендорено minimp3 (public domain)
```

## Замечания

- Перевод чата и ответы ИИ ходят через Google-эндпоинты и `text.pollinations.ai` — без регистрации, всё бесплатно.
- Строгие авто-золота/авто-хук тут нет: хук может дотянуться, только если ты сам нажимаешь клавишу.
- На публичных серверах DDNet, кроме подсказок прицела, читерского ничего нет — но правила серверов читай сам.

## Лицензии

- DDNet и весь базовый клиентский код — zlib (`license.txt`), конtributors DDRaceNetwork.
- `src/engine/external/whisper/` — MIT (whisper.cpp, Georgi Gerganov), модель — MIT (OpenAI Whisper weights).
- `src/engine/external/minimp3/` — public domain (lieff).
- Звуковой движок использует wavpack/OpenAL/etc. — см. их лицензии в соответствующих репозиториях.
