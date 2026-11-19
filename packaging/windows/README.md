# Windows x64: portable ZIP и установщик

Используйте **x64 Native Tools Command Prompt for VS 2022** и закреплённый vcpkg из [инструкции сборки](../../README.md#сборка-на-windows). Нужны CMake/CPack ≥ 3.24, Ninja, системный PowerShell или PowerShell 7. Для установщика — Inno Setup 6.3+ с русским языковым файлом (`ISCC.exe` в PATH или в `Program Files (x86)\Inno Setup 6`). Проверяйте условия выбранной версии компилятора и списка разрешённых к распространению CRT.

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
* `THIRD_PARTY_NOTICES.md`, `LICENSE`, `LICENSES/`, `RUNTIME_INVENTORY.md` с хешами файлов. `windeployqt` берётся из того же triplet, сторонние зависимости определяются по PE-импортам. Тесты, отладочные DLL, чужие CAD SDK, DWG-конвертеры и драйверы не включаются.

Установка для текущего пользователя, без прав администратора. Пункт для `.dxf` **выключен по умолчанию**. При включении PlaneDraft появляется в «Открыть с помощью»; ни установщик, ни деинсталлятор не перезаписывают системный выбор другой САПР. Portable ZIP реестр не меняет.

Для проверки в [CI](../../.github/workflows/ci.yml) предусмотрен `packaging/windows/smoke-test.ps1`. Его можно повторить **на одноразовой Windows-машине с чистым профилем** после упаковки: чистый PATH, обе формы поставки, запуск приложения `--version` без GPU, PDF из примера, тихая установка, проверка обоих вариантов регистрации `.dxf` и удаление. Это не тест Windows-драйвера и не подпись или публикация релиза. Перед внешней поставкой пройти [лицензионный рубеж](../../docs/LICENSING.md).
