[English](README.md) · **Русский**

[![GitHub Actions CI](https://github.com/Alchimon-Code/FreeCAD/actions/workflows/ci.yml/badge.svg?branch=arena%2F85208daa-freecad)](https://github.com/Alchimon-Code/FreeCAD/actions/workflows/ci.yml) [![Лицензия хоста](https://img.shields.io/badge/license-LGPL--2.1--or--later-blue)](LICENSE) [![Лицензия SDK](https://img.shields.io/badge/SDK-MIT-blue)](LICENSES/MIT.txt) ![C++20](https://img.shields.io/badge/C%2B%2B-20-325d88) ![Qt 6](https://img.shields.io/badge/Qt-6.5%2B-368143) ![OpenGL 4.5](https://img.shields.io/badge/OpenGL-4.5_Core-45727c)

# PlaneDraft 2D

![Тёмное рабочее место: два открытых DXF, групповой выбор вставок, свойства, слои и командная строка](docs/images/workspace-dark-ru.png)

PlaneDraft — настольная 2D-САПР для Windows x64 и Linux. Геометрия редактируется в модели, масштабные виды размещаются на листе, а векторный PDF можно выпустить из того же чертежа даже без Qt и видеокарты. Интерфейс сделан по образцу AutoCAD: лента из пяти вкладок без рамок и карточек, командная строка, которая сама себя называет, объектная привязка, называющая пойманную точку, классические команды с алиасами EN и RU. Это самостоятельный проект в `Alchimon-Code/FreeCAD`, а не рабочая среда upstream FreeCAD.

**Попробуйте:** откройте [panel.dxf](examples/drawings/panel.dxf), выберите центральную окружность и дважды щёлкните по **Радиусу** в доке «Свойства». Измените значение и нажмите **Ctrl+Z**. Под холстом переключитесь с **Модель** на **Панель**: на листе А4 в масштабе 1:2 ширина панели стала 160 мм, а размер по-прежнему показывает 320. На снимке сверху выбраны две вставки: у общего поля с несовпадающими значениями написано `*РАЗЛИЧНЫЕ*`.

## От модели до листа

| Сценарий | Возможности |
|---|---|
| [Модель и слои](docs/images/workspace-light-en.png) | Отрезки, окружности, дуги, полилинии с дуговыми участками и текст. Общие свойства выбора меняются одной транзакцией. Заблокированный слой запрещает правку, но не привязку. У каждой вкладки свои выбор, камера, командная строка и история отмены. |
| [Точная правка](editor/README.md) | Выбор до или после команды. Аналитические Trim/Extend по конечным границам, Offset кривых и Fillet двух отрезков с заданным радиусом. Ручки и привязки к концам, серединам, центрам, квадрантам, пересечениям, точкам вставки и ближайшей точке. F3 — привязки, F8 — Орто, F10 — полярное отслеживание. |
| [Блоки и оформление](docs/DRAWINGS.md) | Вложенные определения блоков, вставка и расчленение, штриховка замкнутых границ с островами. Линейные, параллельные, радиальные, диаметральные и угловые размеры; настройки ISO и ЕСКД. |
| [Листы](docs/images/layout-eskd.png) | Виды модели с отсечением по прямоугольнику и явным масштабом, листы А4–А1, рамка и основная надпись по образцу ЕСКД. Переключение на лист не меняет геометрию. |
| [Вывод](docs/images/pdf-output.png) | Чтение и запись поддерживаемого подмножества ASCII DXF. SVG и PDF сохраняют слои, цвета и физический вес линий. **DWG работает «из коробки»:** диалоги открытия и сохранения принимают `*.dwg *.dxf`, а закреплённые GPLv3-программы LibreDWG `dwg2dxf`/`dxf2dwg` поставляются в `tools/libredwg/` и запускаются отдельными процессами — ничего не нужно устанавливать и настраивать. |
| [Рабочее место](editor/README.md) | Лента из пяти вкладок (Главная, Вставка, Аннотации, Вид, Управление), панель быстрого доступа и выпадающие списки слоя, цвета и веса линии; в меню «Вид» лента переключается на классические панели. Пробел подтверждает шаг или повторяет команду, F2 сворачивает историю команд до одной строки и разворачивает обратно, Ctrl+0 оставляет только чертёж, двойной средний клик показывает границы, контекстное меню правой кнопки содержит повтор, недавние команды, буфер и свойства, а F12 включает динамический ввод длины и угла у перекрестия. |
| [Команды](editor/README.md) | Классические имена AutoCAD с алиасами EN и RU: `_RECTANG`/`ПРЯМОУГ`, `_POLYGON`/`МН-УГОЛ`, `_CIRCLE`, `_ARC`, `_PLINE`, `_CHAMFER`/`ФАСКА`, `_ARRAY`/`МАССИВ` (одна отмена), `_MATCHPROP`/`КОПИРОВАТЬСВ`, `_DIST`/`ДИСТ`, `_AREA`/`ПЛОЩАДЬ`, а также `COPYCLIP`/`CUTCLIP`/`COPYBASE`/`PASTECLIP` на Ctrl+C/X/Shift+C/V. Пользовательский `aliases.ini` расширяет встроенную таблицу. |
| [Расширения](sdk/README.md) | Доверенная DLL/SO или [скрипт PlaneDraft Lisp](examples/scripts/pattern.pdl) получает активный документ. Изменения проходят через те же транзакции, что и встроенные команды. |

![Лист А4 с видом в масштабе 1:2, штриховкой сечения А–А, рамкой и основной надписью](docs/images/layout-eskd.png)

Рабочее место v0.2.0 — снимки запущенного приложения, а не рисунки:

| Лента, динамический ввод, DWG |
|---|
| ![Страница «Главная» ленты в тёмной русской теме](docs/images/ribbon-dark-ru.png) |
| ![Страница «Вставка» ленты в тёмной русской теме](docs/images/ribbon-insert-ru.png) |
| ![Страница «Вид» с пиктограммами средств черчения, тёмная русская тема](docs/images/ribbon-view-ru.png) |
| ![Страница «Аннотации» ленты в светлой английской теме](docs/images/ribbon-light-en.png) |
| ![Поля длины и угла рядом с перекрестием при построении отрезка](docs/images/dynamic-input.png) |
| ![Файл DWG, записанный и прочитанный поставляемыми конвертерами, открыт во вкладке](docs/images/dwg-open.png) |

[Светлая тема и английский интерфейс](docs/images/workspace-light-en.png) · [Превью PDF при 144 dpi](docs/images/pdf-output.png) · [Как воспроизвести снимки](docs/images/README.md)

## Зачем так устроено

* **Одна модель без зависимости от окна.** [`pd_core`](core/README.md) не знает ни о Qt, ни об OpenGL. Редактор, конвертер, тесты и расширения работают с одной геометрией и одинаковыми правилами валидации. PDF строится из модели, а не из скриншота.
* **Точность при больших координатах.** [Рендерер](render/README.md) сначала вычисляет `точка − начало пакета` в `double` на CPU и только затем переводит результат в `float32`. Смещение начала пакета относительно камеры тоже рассчитывается в `double`. При панораме около `(10⁷, −10⁷)` видеокарте не приходится вычитать два уже округлённых мировых числа: для этого не нужны 64-битные шейдеры.
* **Отменяются изменения, а не вычисления.** Документ хранит неизменяемые версии сущностей (`shared_ptr<const T>`) со стабильными ID. Транзакция подготавливает замены и копирует лишь изменяемые пути персистентного R-tree. Геометрия, индекс и история становятся видны одновременно. Undo восстанавливает прежние значения без накопления ошибок обратных преобразований; неудачная правка не уничтожает Redo.
* **Плагину не нужен C++ ABI хоста.** [C ABI v2](sdk/include/planedraft/pd_plugin_api.h) передаёт таблицы с размерами, POD, непрозрачные дескрипторы и снимки данных. Сохраняется совместимость с v1. Встроенный [PlaneDraft Lisp](scripting/README.md) не требует Python, Scheme или отдельной среды исполнения.
* **Явные границы поставки.** Хост — [LGPL-2.1-or-later](LICENSE), SDK и примеры — [MIT](LICENSES/MIT.txt); динамические библиотеки Qt заменяемы. DWG-мост поставляет GPLv3-программы LibreDWG `dwg2dxf`/`dxf2dwg` отдельными исполняемыми файлами в `tools/libredwg/` вместе с текстом лицензии: код LibreDWG не линкуется с LGPL-ядром и не загружается им. Граница процесса сама по себе не отменяет лицензионных обязательств. См. [реестр компонентов](THIRD_PARTY_NOTICES.md) и [проверку перед выпуском](docs/LICENSING.md).

## Быстрый старт

Для интерфейса нужен драйвер настольного **OpenGL 4.5 Core**. `pdconvert` и тестам ядра он не нужен. Для переносимой версии Windows распакуйте **весь каталог** и запустите `bin\PlaneDraft.exe`; альтернатива — установщик для текущего пользователя. Для сборки из тега:

```sh
git clone --depth 1 --branch v0.1.0 https://github.com/Alchimon-Code/FreeCAD.git PlaneDraft
cd PlaneDraft
```

### Сборка на Windows

Установите Visual Studio 2022 с **Desktop development with C++**, Windows SDK, CMake ≥ 3.24, Ninja и Git. Откройте **x64 Native Tools Command Prompt for VS 2022** (`cmd.exe`):

```bat
git clone https://github.com/microsoft/vcpkg.git ..\vcpkg
git -C ..\vcpkg checkout 84bab45d415d22042bd0b9081aea57f362da3f35
call ..\vcpkg\bootstrap-vcpkg.bat -disableMetrics
for %I in (..\vcpkg) do set "VCPKG_ROOT=%~fI"
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
build\windows-msvc\bin\PlaneDraft.exe examples\drawings\panel.dxf
```

Первый configure собирает закреплённые зависимости. Для сборки без Qt есть `windows-core`. [Инструкция по Portable ZIP и установщику Inno Setup](packaging/windows/README.md); регистрация `.dxf` в «Открыть с помощью» выключена по умолчанию.

### Сборка на Linux

Нужны GCC или Clang с C++20, CMake ≥ 3.24, Ninja, GoogleTest и Qt **6.5+** с Widgets, OpenGL, OpenGLWidgets, Svg, LinguistTools и каталогом `qtbase_ru.qm`. Если Qt установлен отдельно, укажите его префикс в `CMAKE_PREFIX_PATH`. Чтобы выбрать Clang, задайте `CC=clang CXX=clang++` при первой конфигурации в чистом каталоге сборки.

```sh
cmake --preset linux-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release
build/linux-release/bin/PlaneDraft examples/drawings/cover.dxf examples/drawings/panel.dxf
```

### Демо и выпуск PDF

В `build/<preset>/bin/` или в `bin/` готового комплекта запустите `PlaneDraft --viewport-demo` для сцены в больших координатах. `PlaneDraft --load-plugins` явно разрешает загрузить доверенные модули из соседнего `plugins/`: введите `_FRAME 100` и нажмите **Ctrl+Shift+E**. Без этого ключа каталог автоматически не сканируется. Для первого скрипта выберите **Файл → Запустить скрипт** и откройте [pattern.pdl](examples/scripts/pattern.pdl).

Из корня проекта или готового комплекта, добавив соответствующий `bin` в `PATH`:

```sh
pdconvert --layout "Панель" examples/drawings/panel.dxf panel-A4.pdf
pdconvert examples/drawings/cover.dxf cover.svg
```

[Контракт ввода-вывода и настройка DWG](io/README.md) · [Руководство по выпуску чертежа](docs/DRAWINGS.md). Неизвестные записи DXF отражаются в диагностике; сохранение произвольного стороннего файла без потерь не гарантируется. Размеры и штриховки редактируемы, но не связаны постоянными ограничениями с соседней геометрией.

## Проверка и релизы

В [GitHub Actions](https://github.com/Alchimon-Code/FreeCAD/actions/workflows/ci.yml) настроены Linux GCC/Clang с чтением реального framebuffer Mesa (100%, 150%, 200%), headless ASan/UBSan и MSVC x64. Независимый аудит проверяет структуру DXF и масштаб в растеризованном PDF. Для Windows предусмотрены запуск с чистым PATH, конвертация, установка и удаление, сверка SHA-256 **всех** файлов исполнения с `RUNTIME_INVENTORY.md`. CI не заменяет проверку драйвера на рабочей станции.

**Windows x64 v0.2.0: DWG работает «из коробки».** [Все пять заданий CI прошли](https://github.com/Alchimon-Code/FreeCAD/actions/runs/44722807453). Задание Windows запустило поставляемый `dxf2dwg.exe` на примере, выполнило обмен `panel.dxf → .dwg → .dxf` и проверило, что сущности сохранились: конвертеры найдены рядом с исполняемым файлом, `PATH` чистый. В реестре **27 файлов исполнения**: прежние 25, а также `tools/libredwg/dwg2dxf.exe` и `dxf2dwg.exe`, каждый с текстом `LICENSES/LibreDWG-0.14-GPL-3.0.txt`. Эти две GPLv3-программы собраны из закреплённого тарбола MinGW-w64 (сборка MSVC того же кодировщика не завершается) и не линкуются в LGPL-хост.

**Windows x64 v0.1.0: проверено на MSVC 2022.** [Все пять заданий CI прошли](https://github.com/Alchimon-Code/FreeCAD/actions/runs/44516861855). [Задание Windows](https://github.com/Alchimon-Code/FreeCAD/actions/runs/44516861855/job/135535007043) запустило переносимую и установленную копии с чистым `PATH`, выпустило лист 297 × 210 мм, проверило необязательную регистрацию DXF и удаление. В фактическом реестре **25 файлов исполнения**: 4 файла PlaneDraft, 8 DLL Microsoft CRT, 7 библиотек Qt 6.9.3 (включая Network и SVG), `qwindows.dll`, `qtbase_ru.qm` и DLL double-conversion, libpng, PCRE2-16, zlib. GTest и конвертеры DWG не поставляются.

[Черновик v0.2.0](https://github.com/Alchimon-Code/FreeCAD/releases/tag/v0.2.0): `PlaneDraft-0.2.0-windows-x64.zip`, `PlaneDraft-0.2.0-windows-x64-setup.exe`, а также `SHA256SUMS` и `RUNTIME_INVENTORY.md` той же сборки — файлы взяты из проверенного прогона, без пересборки. Он остаётся черновиком, пока не решён [вопрос условий публичной поставки](docs/LICENSING.md); подготовка черновика этот рубеж не снимает.

[Изменения](CHANGELOG.md) · [Релизы](https://github.com/Alchimon-Code/FreeCAD/releases) · [Карта архитектуры](ARCHITECTURE.md) · [Участие и тесты](CONTRIBUTING.ru.md)
