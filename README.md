# PLCJS Module Tool

Десктопная утилита (Qt6 Widgets, C++) для работы с модулями PLCJS ETH
(STM32F407). Одно окно с четырьмя вкладками:

- **Обнаружение** — поиск модулей по протоколу PLCJS Discovery Protocol (PDP,
  UDP-широковещание, порт 20556). Рассылает `IDENTIFY` через выбранный сетевой
  адаптер и показывает все откликнувшиеся модули **по MAC**, независимо от их
  IP/подсети — поэтому находятся даже устройства «из коробки» (link-local
  `169.254.x.y`) или ошибочно настроенные на чужую подсеть. Выбранному модулю
  можно вживую назначить сеть (статический/DHCP/link-local), задать имя,
  мигнуть опознавательным светодиодом, перезагрузить или сбросить к заводским.
  - **Важно:** ответы приходят UDP-широковещанием, поэтому в брандмауэре Windows
    нужно разрешить **входящий UDP-порт 20556** (иначе список останется пустым).
    Разово, из PowerShell с правами администратора:
    ```powershell
    New-NetFirewallRule -DisplayName "PLCJS PDP discovery" -Direction Inbound `
        -Protocol UDP -LocalPort 20556 -Action Allow
    ```
- **Онлайн** — просмотр/чтение карты регистров. Селектор «Карта модуля»
  выбирает режим:
  - *Свободная* — 16 строк с редактируемыми адресами (по умолчанию 0..11),
    чтение input (FC04) или holding (FC03), запись holding (FC06).
  - *12DI / 12DO / 4RTD* — именованные карты регистров: у каждой строки своя
    метка, тип регистра (FC03/FC04), расшифровка значения (температура в °C,
    режим LED, DHCP, Module ID, маски DI/DQ, режимы выходов, типы датчиков RTD
    и т. п.) и запись только для доступных на запись регистров.
  - Карта *4RTD* содержит значения `float32` (2 регистра, старшее слово
    первым) — они читаются и записываются как десятичные числа (тип `FC0x·f32`).
- **Настройки** — конфигурация каналов/фильтров для модулей 12DI, 12DO, 4RTD,
  4AIU, 4AIC, 4AO. Пока каркас-заглушка (карты регистров будут добавлены).
- **Обновление FW** — выбор `.bin`, перевод модуля в bootloader, прошивка по
  Modbus TCP (BEGIN → блоки → FINALIZE → INSTALL). Поля IP приложения и
  загрузчика, прогресс и журнал.

Протокол Modbus TCP и последовательность OTA портированы из
`../fw_update.mjs`. Modbus-клиент — собственная минимальная реализация поверх
`QTcpSocket` (без внешних зависимостей).

## Сборка

Требуется Qt6 (Widgets, Network), CMake ≥ 3.19, компилятор C++17.

На машине разработчика (Qt в `C:\Qt`):

```powershell
$env:Path = "C:\Qt\Tools\mingw1310_64\bin;" + $env:Path
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" -B build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe" `
    "-DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64" `
    "-DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe" `
    "-DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe"
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build
```

Либо просто открыть `CMakeLists.txt` в Qt Creator (kit Desktop Qt 6.11.1
MinGW 64-bit) и собрать.

### Статическая сборка (один самодостаточный `.exe`, ~6 МБ)

Для распространения собирается один `.exe` без внешних зависимостей, слинкованный
со **статическим** Qt, оптимизированным под размер. Итог — ~6 МБ (против ~28 МБ
у обычной статической сборки).

Использованный статический Qt (qtbase 6.11.1) сконфигурирован под размер:

```
configure -static -static-runtime -release -platform win32-g++ `
    -no-opengl -no-openssl -optimize-size -no-feature-gif -no-feature-jpeg `
    -nomake examples -nomake tests -prefix D:/qt-static/install-size `
    -- "-DCMAKE_CXX_FLAGS=-ffunction-sections -fdata-sections" `
       "-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections"
```

Ключевые приёмы уменьшения: `-optimize-size` (`-Os`), `-ffunction-sections`
(вместе с `-Wl,--gc-sections` на стороне приложения удаляются неиспользуемые
функции), отключение форматов GIF/JPEG. LTO (`-ltcg`/`-flto`) **не используется**:
встроенный MinGW GCC 13.1.0 падает с internal compiler error.

Сборка приложения против такого Qt (генератор Ninja):

```powershell
$env:Path = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\upx;" + $env:Path
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" -B build-static -G Ninja `
    "-DCMAKE_BUILD_TYPE=MinSizeRel" `
    "-DCMAKE_PREFIX_PATH=D:/qt-static/install-size" `
    "-DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe" `
    "-DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe" `
    "-DCMAKE_RC_COMPILER=C:/Qt/Tools/mingw1310_64/bin/windres.exe"
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build-static
```

Финальный `.exe` дополнительно сжимается упаковщиком **UPX** (`--best --lzma`)
автоматическим post-build шагом, если `upx` доступен в `PATH` (или в
`C:\Qt\Tools\upx`). UPX — свободное ПО (GPLv2+), лицензия явно разрешает сжатие
коммерческих программ; исполняемый файл самораспаковывается в память при запуске.
Отключить сжатие: `-DMODULE_TOOL_UPX=OFF`.

## Запуск

```powershell
.\build\module_tool.exe
```

При запуске вне Qt Creator добавьте в `PATH` каталог `C:\Qt\6.11.1\mingw_64\bin`
(или используйте `windeployqt` для автономного распространения).

## Значения по умолчанию

| Параметр            | Значение          |
|---------------------|-------------------|
| IP приложения       | 192.168.1.10    |
| IP загрузчика       | 192.168.1.2    |
| Порт Modbus TCP     | 502               |
| Порт PDP (discovery)| 20556 (UDP)       |
| Unit ID             | 1                 |
| Product ID          | из заголовка образа (fallback 0x504C1201) |
| Макс. блок передачи | 240 байт          |
