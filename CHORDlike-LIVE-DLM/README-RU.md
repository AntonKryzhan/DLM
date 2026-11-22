# CHORD-like LIVE v0.17.0 — расширенный перенос DSP на DLM

## Статус

**Это рабочая нативная DSP-библиотека на DLM v0.133.0, но пока не VST3-плагин.** Не переименовывайте DLL в `.vst3` и не копируйте её в папку VST3: в архиве нет интерфейсов Steinberg `GetPluginFactory`, `IComponent`, `IAudioProcessor`, `IEditController`, `IPlugView`, параметрической автоматизации и редактора DAW.

Все рабочие алгоритмы обработки находятся в `src/live_core.dlm`. Исходные C++ файлы из `tests/reference_*.cpp` применяются **только** для сравнения с оригиналом; в DLM-библиотеку они не включаются.

## Что реализовано

- LIVE FIR с oversampling 2×, 4×, 8×, 16×, 32× и 64×; базовая задержка 157 сэмплов.
- Быстрая предварительно свёрнутая FIR-ветка (folded) для нейтрального тракта, с переключением между обычной и ускоренной обработкой.
- Кроссфид, сглаживание, dry/wet, усиление, фазовые и творческие фильтры.
- Режимы Pulse Array и DAC, dither, noise shaping и детерминированный генератор.
- Шесть ламповых моделей, обе топологии и отдельные низко-, средне- и высокочастотные ламповые каскады с LR4-разделением.
- Переходы между режимами передискретизации, Pulse Array и DAC без пересоздания движка.
- Выходной тракт: auto level match, оценка true peak и lookahead limiter; полная задержка 173 сэмпла.
- Явное создание, сброс и уничтожение экземпляра; обработка в протестированных режимах без выделений памяти.

**Ограничения:** не подтверждена реальная работа в DAW, производительность самых тяжёлых режимов на Windows и полная побитовая эквивалентность SIMD-пути 32×/64× с активными лампами. Сравнения с C++ проводились на Linux x86-64 в конкретных проверочных сценариях.

## Сборка под Windows

Из Windows PowerShell:

```powershell
$env:Path = 'M:\users\Ark\Desktop\DLM\DLM-OUT\bin;M:\users\Ark\Desktop\DLM\DLM-OUT\toolchain\mingw64\bin;' + $env:Path
cd 'M:\users\Ark\Desktop\DLM\CHORDlike-LIVE-DLM'
dlm --version
gcc --version
.\build-windows.cmd
```

Результат: `dist\windows-x86_64\CHORDlike-LIVE-DLM.dll`. Для этого требуется DLM v0.133.0 или новее и GCC MinGW-w64. Скрипт собирает библиотеку из исходного `src/live_core.dlm`; готовая Linux `.so` под Windows не используется.

## Проверка обработки WAV

В **том же** PowerShell, где находятся DLM и GCC:

```powershell
py -3 tools\render_wav.py tests\input_example.wav output-windows.wav --library dist\windows-x86_64\CHORDlike-LIVE-DLM.dll --oversampling 3 --dac 2 --character 0.4 --pulse-color 0.5 --limiter
```

Скрипт автоматически добавляет каталог `gcc.exe` в пути поиска зависимых DLL Windows (Python 3.8+). Для обработки необходимо несжатое стерео WAV PCM16. `--oversampling 3` означает 16×.

Пример с трёхполосными лампами:

```powershell
py -3 tools\render_wav.py tests\input_example.wav output-tubes.wav --library dist\windows-x86_64\CHORDlike-LIVE-DLM.dll --oversampling 3 --tube-routing 1 --band-low '2,0,14.1,0.408,0,0.696' --band-mid '1,0,14.8,0.368,0,0.616' --band-high '3,0,27.6,0.44,0,1' --low-hz 440 --high-hz 3232 --limiter
```

Синтаксис настройки каскада: `tubeType,topology,driveDb,bias,sag,wet`. Полный список параметров: `py -3 tools\render_wav.py --help`.

## Проверка на Linux

```sh
./build-linux.sh
python3 tests/test_port.py
python3 tests/compare_live.py --reference-dir /path/to/CHORDlike-original
python3 tests/test_output_stage.py --reference-dir /path/to/CHORDlike-original
python3 tests/test_tube.py --reference-dir /path/to/CHORDlike-original
python3 tests/test_threeband.py --reference-dir /path/to/CHORDlike-original
python3 tests/test_mode_transition.py --reference-dir /path/to/CHORDlike-original
python3 tests/test_folded.py --reference-dir /path/to/CHORDlike-original
python3 tests/test_folded_toggle.py --reference-dir /path/to/CHORDlike-original
```

Для сравнений нужен **отдельный исходный архив оригинального CHORDlike v0.17.0**, содержащий `Source/DSP/*.cpp`; он намеренно не дублируется внутри DLM-проекта. Подробный протокол — `docs/VALIDATION.md`, сигнатуры C ABI — `include/CHORDlikeLiveDLM.h`.

## Что не сделано

Полная Steinberg VST3-обвязка, точное соответствие параметров/пресетов/автоматизации, встраивание DLM UI в DAW, сборка `.vst3`, запуск VST3 Validator и проверки в Windows DAW. Указанный функционал необходимо реализовать и проверить до заявления о 100% готовности плагина.
