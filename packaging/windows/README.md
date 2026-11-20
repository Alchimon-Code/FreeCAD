# Windows x64: portable ZIP и установщик

Используйте **x64 Native Tools Command Prompt for VS 2022** и закреплённый vcpkg из [инструкции сборки](../../README.ru.md#сборка-на-windows). Нужны CMake/CPack ≥ 3.24, Ninja, системный PowerShell или PowerShell 7. Для установщика — Inno Setup 6.3+ с русским языковым файлом (`ISCC.exe` в PATH или в `Program Files (x86)\Inno Setup 6`). Проверяйте условия выбранной версии компилятора и списка разрешённых к распространению CRT.

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
:: Только ZIP:
cpack --config build\windows-msvc\CPackConfig.cmake -G ZIP -C Release
:: Или сразу оба файла (установщик читает распакованный ZIP):
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\make-installer.ps1
```

Результат в `build/windows-msvc/packages/`: `PlaneDraft-<версия>-windows-x64.zip` и `PlaneDraft-<версия>-windows-x64-setup.exe`. Для осмотра дерева без упаковки: `cmake --install build/windows-msvc --prefix C:/Temp/pd-stage` (новый пустой каталог). Рабочий каталог приложения произвольный.

* `bin/PlaneDraft.exe`, `bin/pdconvert.exe`, сменяемые Qt DLL, `platforms/qwindows.dll`, `translations/qtbase_ru.qm`, релизный MSVC CRT; для холста нужен системный драйвер настольного OpenGL 4.5 Core.
* `plugins/` — только `sample_line.dll` и `frame_generator.dll`, загрузка по явному запросу; `examples/scripts/pattern.pdl`, `examples/drawings/{cover,panel}.dxf`, `examples/aliases.ini`, `docs/DRAWINGS.md`.
* `include/pd_plugin_api.h` — самостоятельный заголовок v2; `include/planedraft/` — пути для существующих примеров v1/v2. Заголовки и примеры — MIT.
* `THIRD_PARTY_NOTICES.md`, `LICENSE`, `LICENSES/`, `RUNTIME_INVENTORY.md` с хешами файлов. `windeployqt` берётся из того же triplet, сторонние зависимости определяются по PE-импортам. Тесты, отладочные DLL, чужие CAD SDK и драйверы не включаются. В комплект входят два GPLv3-исполняемых файла LibreDWG (`tools/libredwg/dwg2dxf.exe`, `dxf2dwg.exe`) и текст `LICENSES/LibreDWG-0.14-GPL-3.0.txt`: они запускаются отдельными процессами, поэтому GPLv3 остаётся вне LGPL-ядра.

Эти две программы собирает **не MSVC**: компилятор Microsoft превращает кодировщик LibreDWG 0.14 в бесконечный цикл (проверено и внутри проекта, и на отдельной сборке того же тарбола, Release, /O2), тогда как сборка MinGW-w64 из тех же исходников работает так же, как сборка Linux. Поэтому `packaging/windows/build-converters.ps1` скачивает закреплённый тарбол, сверяет SHA-256, собирает `dwg2dxf`/`dxf2dwg` статически MinGW-w64 и раскладывает их в `build/converters/dist` вместе с `COPYING`. Конфигурация получает `-DPD_LIBREDWG_PREBUILT_DIR=<этот каталог>` и только копирует готовые файлы: `cmake/libredwg.cmake` в этом режиме ничего не скачивает и не собирает. Минимальная версия MinGW — та, что в образе `windows-2022` (`C:\mingw64`); путь можно переопределить параметром `-Mingw`.

Установка для текущего пользователя, без прав администратора. Пункт для `.dxf` **выключен по умолчанию**. При включении PlaneDraft появляется в «Открыть с помощью»; ни установщик, ни деинсталлятор не перезаписывают системный выбор другой САПР. Portable ZIP реестр не меняет.

Для проверки в [CI](../../.github/workflows/ci.yml) предусмотрен `packaging/windows/smoke-test.ps1`. Его можно повторить **на одноразовой Windows-машине с чистым профилем** после упаковки (для проверки ссылок нужен Python ≥ 3.9): чистый PATH, сверка SHA-256 всех файлов исполнения в распакованном и установленном комплектах, запуск приложения `--version` без GPU, PDF формата А4 с листа «Панель», круговой обмен DWG (DXF → DWG → DXF) встроенными конвертерами, тихая установка, проверка обоих вариантов регистрации `.dxf` и удаление. Это не тест Windows-драйвера и не подпись или публикация релиза. Перед внешней поставкой пройти [лицензионный рубеж](../../docs/LICENSING.md).

### Перенос проверенного комплекта в черновик релиза

CI сохраняет ZIP, установщик, извлечённый из ZIP `RUNTIME_INVENTORY.md` и `SHA256SUMS` в артефакте `PlaneDraft-windows-x64-<полный SHA>`. В сводке Windows-задания есть тот же реестр после сверки файлов. После успешного **push**-прогона и слияния проверенного коммита в основную ветку сопровождающий может выполнить `gh workflow run draft-release.yml -f run_id=НОМЕР_ПРОГОНА`. [Задание подготовки релиза](../../.github/workflows/draft-release.yml) проверяет происхождение, коммит и контрольные суммы, берёт описание из CHANGELOG проверенной ревизии и прикрепляет уже испытанные файлы **без пересборки**. Черновик не публикуется автоматически; существующий тег/релиз не перезаписывается.

Для первого релиза до слияния есть явный путь в той же ветке: после **зелёного** CI текущего HEAD и проверки реестра владелец репозитория делает пустой коммит `git commit --allow-empty -m "Draft verified PlaneDraft release from run НОМЕР"` и отправляет его в эту же ветку. Принимается только **непосредственный родитель** с тем же деревом, из той же ветки и успешного push-прогона. Тег указывает на испытанный родитель, а не на пустой запрос. Обычный push не создаёт ни тега, ни релиза.
