# Project Dependencies

## Vendored Into This Project

The project no longer depends on `..\DeviceDownload111\...`.
All SDK source/header dependencies used by the current build are now stored under:

- [vendor/nRF5_SDK_17.1.0](C:/Users/H2Q/Downloads/nrf52840_hotpatch_platformio/vendor/nRF5_SDK_17.1.0)

Vendored SDK subtrees:

- `components`
- `external/segger_rtt`
- `integration/nrfx`
- `modules/nrfx`

These are referenced by:

- [platformio.ini](C:/Users/H2Q/Downloads/nrf52840_hotpatch_platformio/platformio.ini)
- [sdk_src/segger_rtt_core.c](C:/Users/H2Q/Downloads/nrf52840_hotpatch_platformio/sdk_src/segger_rtt_core.c)
- [sdk_src/segger_rtt_printf.c](C:/Users/H2Q/Downloads/nrf52840_hotpatch_platformio/sdk_src/segger_rtt_printf.c)

## Host-Side Tools Still Required

These are not copied into the project folder:

- PlatformIO
- `toolchain-gccarmnoneeabi`
- `tool-jlink` or another accessible `JLink.exe`

[platformio_extra.py](C:/Users/H2Q/Downloads/nrf52840_hotpatch_platformio/platformio_extra.py) now resolves `JLink.exe` in this order:

1. PlatformIO package `tool-jlink`
2. `JLink.exe` from `PATH`

That removes the previous hardcoded dependency on `C:\Program Files\SEGGER\...`.

## Result

Current source-level dependencies are self-contained inside this project directory.
You can move or archive this folder without also carrying `..\DeviceDownload111\...`.
