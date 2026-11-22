# CHORD-like LIVE VST3 на DLM — сборочный комплект

В комплекте — DLM v0.133.0 DSP (`src/live_core.dlm`), собственный векторный редактор (`src/live_editor.dlm`), VST3 C ABI-адаптер и сборочные сценарии. DSP и графика реализованы на DLM; адаптер сохраняет Steinberg ABI и не зависит от JUCE.

## Сборка под Windows

Положи папку `CHORDlike-LIVE-DLM` в `M:\users\Ark\Desktop\DLM\` рядом с `DLM-OUT`. Скрипт сам ищет `DLM-OUT\bin\dlm.exe` и `DLM-OUT\toolchain\mingw64\bin\gcc.exe`.

В PowerShell выполни:

```powershell
cd 'M:\users\Ark\Desktop\DLM\CHORDlike-LIVE-DLM'
.\BUILD-VST3-WINDOWS.cmd
```

После успешной сборки получишь **папку-бандл**, не отдельную DLL:

```
CHORDlike-LIVE-DLM\dist\vst3-windows\CHORDlike LIVE.vst3\
    Contents\x86_64-win\CHORDlike LIVE.vst3
    Contents\x86_64-win\CHORDlike-LIVE-DLM.dll
    Contents\x86_64-win\CHORDlike-LIVE-DLM-UI.dll
    Contents\x86_64-win\[библиотеки MinGW]
```

Скопируй **всю папку** `CHORDlike LIVE.vst3` в `C:\Program Files\Common Files\VST3\`. Перезапусти DAW и обнови список плагинов. В VST3 сканере ищи **CHORD-like LIVE DLM**.

Если возникнет ошибка линковки VST3 на Windows, пришли **полный вывод `BUILD-VST3-WINDOWS.cmd`**. Здесь Win64 GCC не установлен, поэтому Windows бинарник и загрузку в DAW не удалось проверить.

## Что реализовано

- `GetPluginFactory`, два класса: processor/controller.
- Стерео аудио, 32-bit float, 173 samples latency/PDC.
- 48 VST3 параметров с начальными значениями, основанными на LIVE v0.17.0, и уникальными ParamID.
- Перевод автоматизации DAW на границах sample offsets.
- Сохранение состояния через VST3 `IBStream` (DLM-формат, версия 1).
- Вызовы оригинально перенесённого DLM DSP, включая TubeStage, 3-band, DAC/Pulse, oversampling, folded FIR, OutputStage.
- Собственный Windows `IPlugView` с родительским HWND, изменением размера и поддержкой UI-жестов через `beginEdit`/`performEdit`/`endEdit`.
- Векторная сцена DLM: все 48 регуляторов (4×12), mouse drag, колесо, клавиатура, обновление при изменениях из DAW. Индикаторы показывают нормализованное значение в процентах.
- На Linux выполнены тесты фабрики, классов, параметров, обработки 1024 отсчётов, PDC; редактор проверен отдельно под Xvfb.
- Дополнительная проверка Win32 GUI на твоём ПК: `py -3 tools\test_vst3_editor_windows.py`. Она создаёт тестовое родительское окно, прикрепляет IPlugView, имитирует изменение параметра и очищает ресурсы.

## Ограничения относительно исходной версии

- **Windows: собственный редактор DLM доступен через `createView("editor")`.** Дизайн создан заново и не повторяет 1:1 оригинальный интерфейс JUCE. Нет родного браузера пресетов/оригинальных графиков и визуальной схемы из JUCE; все 48 параметров доступны. Linux: встроенный IPlugView пока не подключён (generic UI).
- Class IDs и ParamIDs VST3 новые. **Старые проекты JUCE VST3 не гарантированно совместимы**. Форматы `.chordpreset` и JUCE XML-state не импортируются.
- Поддерживается только planar `float32`, не float64.
- **Новая Windows GUI-сборка не запускалась в DAW в этой среде:** здесь нет MinGW Win64 и Steinberg Validator. Выполнены компиляция DLM-UI на Linux, рисование в Xvfb, проверка Win32-ветки C с `-Werror` (без настоящего Windows SDK) и моделирование жизненного цикла `IPlugView`. Окончательная проверка — сборка и открытие редактора в твоём Windows DAW.
- Подтверждена загрузка и обработка в синтетическом Linux-хосте, но нет гарантии отсутствия аудиодропов на твоём ПК. Нужны измерения CPU 32×/64×.

Не объявляй этот выпуск 100% эквивалентом исходного JUCE-плагина: собственный DLM-редактор реализован, но визуально отличается от оригинала; миграция старого состояния, пресетов и проверка реальной DAW остаются открытыми.

## Windows: проверка нового редактора

Сборка скриптом `BUILD-VST3-WINDOWS.cmd` теперь **дополнительно компилирует DLM UI** в отдельную DLL, включает её в VST3-пакет и выполняет базовый тест наличия интерфейса `IPlugView`. После сборки, перед установкой, в интерактивной сессии Windows выполни:

```powershell
py -3 tools\test_vst3_editor_windows.py
```

Ожидается строка `PASS: VST3 editor attached to HWND ...`. При неуспехе не устанавливай новый плагин вместо рабочего: сначала пришли сообщение теста. Это не заменяет проверки в DAW.

Папку `.vst3` нужно копировать полностью, включая DLM DSP DLL, DLM UI DLL и требуемые библиотеки MinGW. Установка одиночного файла `.vst3` из `Contents` недопустима.

## Linux

`./BUILD-VST3-LINUX.sh` создаёт `dist/vst3-linux/CHORDlike LIVE.vst3`; туда включены бинарные `.so` Linux x86-64. Проверочный хост: `python3 tests/vst3/test_host.py 'dist/vst3-linux/CHORDlike LIVE.vst3/Contents/x86_64-linux/CHORDlike LIVE.so'`.

## Основание ABI

Документация Steinberg: https://github.com/steinbergmedia/vst3_c_api и https://steinbergmedia.github.io/vst3_dev_portal/

Перед распространением третьим лицам убедись в соблюдении лицензий VST3 SDK и проекта.
