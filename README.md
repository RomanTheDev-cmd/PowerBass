# PowerBass

Басовый синтезатор MyDarknessDays для Dark, Hard, Industrial, Underground и Raw Techno, Schranz и Hardgroove. Минимум регуляторов, максимум характера. Это настоящий синтез, без сэмплов.

Форматы:

- **macOS:** AU (для Logic) и VST3, универсальная сборка Apple Silicon + Intel, а также отдельное приложение Standalone.
- **Windows 10/11 x64:** VST3 и отдельное приложение.

## Установка

**macOS.** Из папки `Сборки/macOS`:

- `PowerBass.component` → `~/Library/Audio/Plug-Ins/Components/`
- `PowerBass.vst3` → `~/Library/Audio/Plug-Ins/VST3/`

На этом Mac плагин уже установлен. В Logic: AU Instruments → MyDarknessDays → PowerBass.

**Windows.** Из `Сборки/PowerBass-1.0.0-Windows-x64.zip` скопируйте папку `PowerBass.vst3` целиком в `C:\Program Files\Common Files\VST3\`. Подробности — в `УСТАНОВКА.txt` внутри архива.

## Быстрый путь к басу

1. Выберите пресет.
2. Подберите основу ручкой **SHAPE**.
3. Добавьте металла ручкой **FM AMOUNT**.
4. Задайте движение ручкой **CUTOFF**.
5. Усильте перегруз ручкой **DRIVE**.
6. Поменяйте характер ручками **DARK** и **BITE**.
7. При необходимости расширьте верх ручкой **WIDTH**.

## Регуляторы

| Секция | Ручки | Что делает |
|---|---|---|
| OSC / FM | SHAPE, SUB, DRIVE | SHAPE плавно переходит sine → triangle → saw → square (полосно-ограниченные волны). SUB — синус на октаву ниже. DRIVE — насыщение внутри осциллятора |
| | FM AMOUNT, FM RATIO, FM DECAY, MIX | 2-операторная FM; модулятор с обратной связью. 18 соотношений частот: кратные, дробные, негармонические (×1,41, ×2,76, ×3,14, ×7,13…). FM AMOUNT действует и на аналоговый осциллятор, так что металл добавляется при любом MIX |
| FILTER | LP24 / LP12 / BP / HP, CUTOFF, RESONANCE, ENV AMOUNT | TPT-фильтр, мягкое отслеживание высоты ноты, огибающая со своим затуханием |
| DISTORTION | SATURATE / CLIP / FOLD / DESTROY / CRUSH, DRIVE, TONE, MIX | Ниже 120 Гц сигнал только мягко насыщается, поэтому саб не превращается в гул. CRUSH — намеренное цифровое разрушение (понижение битности и частоты) |
| CHARACTER | DARK, BITE, PUNCH | DARK опускает фильтр и TONE, уплотняет низ-середину (160 Гц), гасит верх. BITE добавляет драйв, резонанс, FM-скрежет и середину 1,8 кГц. PUNCH — короткий удар по высоте и громкости в начале ноты |
| STEREO | WIDTH, SPLIT, DELAY | Всё ниже SPLIT (100–1000 Гц) строго моно. Выше — декоррелированный боковой сигнал: `L = M + S`, `R = M − S`. DELAY (0–15 мс) — настоящий сдвиг между L и R выше SPLIT; моно-сумма от него не меняется |
| ENVELOPE / GLIDE | A, D, S, R, GLIDE, MONO / LEGATO | Монофонический голос с приоритетом последней ноты. LEGATO: перекрывающиеся ноты скользят без перезапуска. MONO: каждая нота перезапускается |
| OUTPUT | MASTER, осциллограф, индикатор | Мягкий связанный ограничитель пиков (одинаковое усиление на оба канала) |

- **Панель ADVANCED:**
  - фильтр до или после дисторшна;
  - oversampling 2x, 4x или 8x;
  - затухание огибающей фильтра;
  - DETUNE: две расстроенные копии аналогового осциллятора (риз);
  - чувствительность к velocity;
  - диапазон pitch bend.
- **SAVE и меню «…»:** сохранение, переименование, удаление (в Корзину), импорт и экспорт пресетов `.pbpreset`. Это XML без абсолютных путей, файлы переносятся между macOS и Windows. Пользовательские пресеты хранятся в `Documents/PowerBass/Presets`.

**300 заводских пресетов** в 10 категориях; внутри каждой — группы Reese / FM / Plucks / Sustained:

- Rolling Techno — 100;
- Dark Underground — 60;
- Hard Techno Rumble — 55;
- Raw Analog — 20;
- Acid-Inspired — 13;
- Destroyed — 13;
- Schranz Distorted — 12;
- Metallic — 12;
- Industrial FM — 10;
- Deep Sub — 5.

## Близость к референсам

295 из 300 заводских пресетов подогнаны к типичным басовым лупам из референсов (паки TSOHT; для Rolling — пул из 107 лупов, отобранных по ритму) с помощью `Tools/fit_bass.py` и собраны `Tools/build_library.py`. Совпадают гармонический профиль относительно ноты, плотность, движение тембра и стерео выше 100 Гц. Ниже 100 Гц бас по заданию строго моно, в отличие от многих широких лупов в паках. Звук референсов в плагин не попадает. Пресеты выдают пик не выше −1 дБFS на любой ноте от E1 до C3 (два риффа и выдержанная нота на каждом полутоне, `Tools/normalize_peaks.py`), поэтому аварийная защита в обычной игре не срабатывает. Высота тона баса пока не менялась.

## Как устроен звук

```
pitch (glide, bend, punch) → analog osc (morph + FM PM) / FM sine + sub → [filter] → distortion (sub-protected,
TONE, MIX) → [filter] → amp ADSR          — всё это в oversampling 2/4/8x
→ DC-фильтр → DARK/BITE EQ → stereo (моно ниже SPLIT) → MASTER → связанный ограничитель пиков
```

**Плавность и безопасность в реальном времени:**

- все непрерывные параметры сглаживаются (~15 мс);
- смена пресета, типа дисторшна, режима или положения фильтра и oversampling проходит через затухание 4 мс, без щелчков;
- в аудиопотоке нет выделений памяти (это проверяется тестом).

## Сборка

Нужны CMake 3.22+ и JUCE 8 (`third_party/JUCE`).

**macOS:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

```bash
build/powerbass_tests_artefacts/Release/powerbass_tests
```

**Windows** (на Windows): Visual Studio 2022 и те же команды.

**Windows** (кросс-сборка на Mac):
- Нужны три инструмента:
  - LLVM (llvm-mingw, только clang-cl в режиме MSVC);
  - Windows SDK, скачанный через `xwin splat`;
  - Wine.
- Задайте переменные окружения:
  - `WIN_LLVM_ROOT` — путь к LLVM;
  - `WIN_SDK` — путь к Windows SDK;
  - `WIN_WINE` — обёртка для запуска wine (через неё сборка генерирует `moduleinfo.json`).
- Сборка:

```bash
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/windows-clang-msvc.cmake -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build-win -j
```

`powerbass_tests --report` выводит уровни пресетов. `--export <папка>` рендерит рифф каждым пресетом в WAV. `Tools/normalize_peaks.py` выставляет MASTER пресетов по пику (запускать после пересборки тестов).

## Референсы

`Tools/analyze_bass.py` измеряет басовые лупы: спектр по полосам, стерео по полосам, основную частоту, crest-фактор и постоянную составляющую. Звук не копируется. Результаты лежат в `Референсы/`: 367 басовых линий и 120 rumble-басов из паков TSOHT, а также наши демо.
