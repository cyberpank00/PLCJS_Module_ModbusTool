# AGENTS.md

Desktop client for the PLCJS ETH I/O module family — Qt6 Widgets, C++17,
Windows. Discovers modules, browses their Modbus register maps and flashes
firmware over Modbus TCP. This file is the orientation map for agents;
user-facing documentation lives in `README.md`.

This is the only x86 project in the workspace; everything else is STM32F407
firmware. It contains **hand-written re-implementations** of protocols defined in
the firmware repos — there is no shared code, so it drifts silently. See
*Multi-repo*.

## Build

Qt6 (Widgets, Network), CMake >= 3.19, C++17. No CMake presets; no external
dependencies beyond Qt (the Modbus client is hand-rolled on `QTcpSocket`).

Developer build (Qt in `C:\Qt`, MinGW):

```powershell
$env:Path = "C:\Qt\Tools\mingw1310_64\bin;" + $env:Path
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" -B build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe" `
    "-DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64" `
    "-DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe" `
    "-DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe"
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build
```

Or open `CMakeLists.txt` in Qt Creator (kit Desktop Qt 6.11.1 MinGW 64-bit).

Release build is a single self-contained `.exe` (~6 MB) linked against a
size-optimised **static** Qt at `D:/qt-static/install-size`, built with
`-DCMAKE_BUILD_TYPE=MinSizeRel`. The exact static-Qt `configure` line and the
build commands are in `README.md` — do not re-derive them.

Two build-system landmines, both already solved; do not "simplify" them:
- **RC compiler.** On Windows the build prefers `llvm-windres` (auto-detected in
  `C:/Qt/Tools/llvm-mingw1706_64/bin`). GNU `windres` silently emits a malformed
  VERSIONINFO block that Explorer cannot read.
- **No LTO.** `-ltcg` / `-flto` is deliberately not used: the bundled MinGW GCC
  13.1.0 dies with an internal compiler error. Size is reclaimed via
  `-optimize-size`, `-ffunction-sections` and `--gc-sections` instead.

`qt_add_executable` (not plain `add_executable`) is required so a static Qt
auto-imports its platform/style plugins.

There are no automated tests. Verification is manual, against a real module.

## Source map (`src/`)

| Path | Responsibility |
|---|---|
| `main.cpp`, `MainWindow.*` | Entry point; a `QTabWidget` with three tabs. |
| `tabs/DiscoveryTab.*` | PDP discovery UI: list by MAC, assign network, set name, flash LED, reboot, factory reset. |
| `tabs/OnlineTab.*` | Register browser. "Карта модуля" selector switches between free-address mode and named per-module maps. |
| `tabs/FwUpdateTab.*` | Firmware-update UI (pick `.bin`, addresses, progress, log). |
| `tabs/FwWorker.*` | The OTA state machine, run off the GUI thread. |
| `maps/ModuleMaps.*` | Named register maps — `build12DI()`, `build12DO()`, `build4RTD()`, `build8AIC()`. |
| `protocol/Pdp.*` | PLCJS Discovery Protocol client (UDP/20556 broadcast). |
| `protocol/BootloaderProtocol.*` | Bootloader OTA register interface. |
| `modbus/ModbusTcpClient.*` | Minimal Modbus TCP client over `QTcpSocket`. |
| `modbus/Crc32.*` | CRC32 for OTA image metadata. |
| `app_version.h`, `version.h` | Version single source of truth (see below). |
| `app.rc`, `resources.qrc` | Windows VERSIONINFO / exe icon; embedded window icon. |

## Invariants

### Version policy — bump the patch on every change

**Mandatory.** Every change to the tool ships with a version bump. Because this
is a hand-distributed `.exe`, the version in the title bar and in the file
properties is the only way to know what a user is running.

`src/app_version.h` is the single source of truth — it is included by C++ **and**
by the Windows resource compiler, so it must contain no Qt types.

Bump checklist — **five** values in that one file, and they must agree:
1. `APP_VERSION_PATCH` (increment by one; or `APP_VERSION_MINOR` for a feature
   release, resetting patch to 0).
2. `APP_VERSION_STR` — e.g. `"1.0.6.0"`.
3. `APP_PRODUCT_VER` — e.g. `"1.00.06"`.
4. `APP_VERSION_MAJOR` / `APP_VERSION_MINOR` / `APP_VERSION_BUILD` as applicable.

The strings are duplicated deliberately: `windres` does not reliably expand
multi-part macro string literals, so they cannot be composed from the numeric
defines. Changing the numbers without the strings produces an `.exe` whose file
properties disagree with its title bar.

A chronological version-review / changelog file is planned; once it exists, add
an entry there in the same commit as the bump.

### Protocol fidelity
`Pdp.cpp` and `BootloaderProtocol.cpp` are ports, not shared code. They must
match the firmware byte-for-byte:

- **PDP:** UDP/20556 broadcast both ways, protocol version 1, 16-byte header,
  response flag `0x80`, fixed **38-byte** IDENTIFY payload with a fixed 16-byte
  name field (15 chars + NUL). Requests are honoured when the target MAC is
  all-zero or the device's own.
- **OTA:** `BEGIN_UPDATE`(1) → write-block → `FINALIZE_UPDATE`(2) →
  `INSTALL_UPDATE`(3) → `REBOOT`(5), with `ABORT_UPDATE`(4). Bootloader magic
  `0xB00710AD`. Block payload capped at 240 bytes (120 registers).

Broadcast replies mean **inbound UDP 20556 must be allowed in the Windows
firewall**, or discovery silently returns nothing. This is the single most common
"the tool is broken" report; the rule is in `README.md`.

### Register maps
`float32` values occupy two registers, **high word first** (`register[N]` = bits
31..16). This matters for the 4RTD map; a wrong word order yields
plausible-looking garbage rather than an error.

Module IDs decoded in `ModuleMaps.cpp` (IR125): `0x12D1` 12DI, `0x12D0` 12DO,
`0x04D1` 4RTD, `0x08AC` 8AIC, `0x04DD` 4RD. These must match `MODULE_ID_*` in each firmware's
`modbus_app.h`.

When a firmware's register map changes, the corresponding `buildXXX()` must be
updated here or the tool shows stale rows. Nothing detects this automatically.

## Known stale documentation

Both are cosmetic but will mislead:

- `README.md` describes **four** tabs including "Настройки" (settings). That tab
  was removed (`UI: remove Settings tab`); `MainWindow.cpp` adds only
  Обнаружение / Онлайн / Обновление FW.
- The header comment in `src/maps/ModuleMaps.h` says 12DO and 4RTD are stubs.
  They are fully implemented — `isStub()` returns `false` unconditionally.

## Multi-repo workspace

Sibling firmware repos under `E:\STM_Programming\`:

| Repo | product_id | IR125 | Mirrored here by |
|---|---|---|---|
| `PLCJS_ETH_MODULE_12DI_D4MG_...` | `0x504C1201` | `0x12D1` | `build12DI()` |
| `PLCJS_ETH_MODULE_12DQ_D4MG_...` | `0x504C1202` | `0x12D0` | `build12DO()` |
| `PLCJS_ETH_MODULE_4RTD_D4MG_...` | `0x504C0403` | `0x04D1` | `build4RTD()` |
| `PLCJS_ETH_MODULE_8AIC_D4MG_...` | `0x504C0804` | `0x08AC` | `build8AIC()` |
| `BOOTLOADER_PLCJS_ETH_MODULE_STM32F407VGT6` | — | — | `BootloaderProtocol.*`, `FwWorker.*` |

The authoritative definitions live in the firmware repos:
- Register maps — the header comment of each firmware's
  `Application/modbus/modbus_app.h`.
- PDP wire format — each firmware's `Application/discovery/discovery.c`.
- OTA protocol — the bootloader's `Application/modbus/fw_update_proto.h`; the
  reference client implementation is its `tools/fw_update.mjs`, which `FwWorker`
  was ported from.
- Flash layout and `fw_header_t` — the bootloader's `Application/flash/flash_map.h`
  and `Application/validate/app_validate.h`.

When a protocol changes, the same change must be made in **three** independent
implementations: the firmware C code, `tools/fw_update.mjs` in the bootloader
repo, and this Qt code. Consult the firmware source rather than guessing from
this repo's ports.

The three firmware variants are now aligned on shared subsystems, and all of them
support HR118 `0x8863` (KSZ8863 switch reset). Their register maps still differ
substantially by function, so do not assume a register exists on every module
just because one map has it.

## Maintaining this file

`AGENTS.md` is a living document, not a one-time write. Update it **in the same
commit** as the change it describes — a stale map is worse than no map, because
it actively misleads. Touch it when:

- a protocol port (`Pdp.cpp`, `BootloaderProtocol.cpp`) changes to match a
  firmware-side wire-format change;
- a register map is added or changed in `ModuleMaps.cpp` (mirror of a firmware
  `modbus_app.h` change);
- the build procedure, static-Qt line or windres/RC landmine changes;
- `APP_VERSION_PATCH` / `APP_VERSION_MINOR` is bumped and the version-policy
  text needs the new example value;
- a module ID is added or a new `buildXXX()` is introduced;
- a cross-repo contract changes (PDP wire format, OTA protocol, `fw_header_t`,
  flash map) — update the *Multi-repo* section here **and** the corresponding
  section in the sibling repo(s).

Pure refactors with no behavioural change do not require an update, but when in
doubt, update — the cost is a few lines of text, the cost of a stale invariant
is a field bug.
