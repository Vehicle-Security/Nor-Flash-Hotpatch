# MorphPatch: NOR-Flash Hotpatching for Embedded Systems

MorphPatch is a runtime hotpatching technique for microcontrollers with NOR flash.
It exploits the monotonic 1-to-0 bit-clear property of NOR flash to redirect execution
from a vulnerable function to a patched replacement **without erasing any flash sector**
and **without halting the running firmware**.

## How It Works

MorphPatch now supports two monotonic dispatch paths from the same factory slot:

- `direct-branch` path: `0xE7FF -> Thumb B(fun2) -> 0xE000`
- `fault-dispatch` path: `0xE7FF -> 0x4700 -> 0x4600`

`0xE7FF` remains the factory image. It falls through to the original `fun1` path.
The original direct-branch path is preserved and remains the default option.

The fault-dispatch path clears the first halfword to `0x4700` (`BX R0`). Its
entry wrapper saves the caller's original `R0` in `R12`, forces `R0 = 0`, and
enters `patch_slot`. That makes `BX R0` raise a controlled exception instead of
branching into arbitrary code. The exception handler restores stacked `R0` from
stacked `R12`, then redirects execution to the fix function. Unpatch clears one
more bit from `0x4700` to `0x4600`, restoring the original path without erase.

You can switch the MorphPatch path at runtime with `path direct` or `path fault`
before applying the patch.

- `path direct`: prefer the original `0xE7FF -> B(fun2) -> 0xE000` path. If the
  direct branch write fails, MorphPatch retries once. If it still cannot be
  programmed monotonically, the install logic automatically falls back to the
  `fault-dispatch` path as a backup without changing the user-selected mode.
- `path fault`: force the `0xE7FF -> 0x4700 -> 0x4600` path from the start.

## Repository Structure

```text
src/                    Source modules
  core/                 MorphPatch core implementation (shared by all ARM targets)
  compare/              Benchmark comparison: MorphPatch vs RapidPatch / HERA / AutoPatch
  realworld/            Real-world LED demo - live hotpatch without disrupting a blinking task
targets/                Hardware targets
  nrf52840/             ARM Cortex-M4: SDK wrappers, linker scripts, config, J-Link tools
  ch32v203/             RISC-V: standalone PlatformIO project (CH32V203C8T6)
  vendor/               Third-party: nRF5 SDK 17.1.0, FreeRTOS-Kernel, LiteOS, NuttX, RapidPatch
ports/                  RTOS port adapters (baremetal, FreeRTOS, LiteOS, NuttX, RT-Thread, Zephyr)
```

## Build Targets (ARM - nRF52840 DK)

All ARM targets are built from the root `platformio.ini`. Install
[PlatformIO](https://platformio.org/) and run:

| Command | Description |
|---|---|
| `pio run -e baremetal` | MorphPatch on bare metal (baseline) |
| `pio run -e freertos` | MorphPatch on FreeRTOS |
| `pio run -e liteos` | MorphPatch on LiteOS |
| `pio run -e nuttx` | MorphPatch on NuttX |
| `pio run -e compare` | Benchmark all four hotpatch schemes |
| `pio run -e realworld` | LED demo, bare metal |
| `pio run -e realworld_freertos` | LED demo, FreeRTOS (press S1 to apply, press again to unpatch) |

Upload to the board:

```bash
pio run -e <env> -t upload
```

Connect to the RTT console with **SEGGER RTT Viewer** (device: nRF52840_xxAA, SWD).

### RTT Console Commands

```text
help        Show available commands
path        Show current MorphPatch path
path <x>    Switch path: direct | fault
status      Print current patch state
patch       Apply the MorphPatch
unpatch     Remove the patch (clear-forward)
call        Execute the patch slot (vulnerable or fixed path)
demo        Run a full patch/unpatch/call cycle
bench       Measure cycle counts for patch operations
target <x>  Switch CVE target (cve2024-2212 | cve2025-1674 | cve2025-12899)
```

The `compare` environment adds:

```text
mode <scheme>   Switch scheme: morphpatch | rapid | hera | autopatch
compare         Run benchmarks for all four schemes and print a comparison table
```

## Build Target (RISC-V - CH32V203C8T6)

The RISC-V port is a standalone PlatformIO project:

```bash
cd targets/ch32v203
pio run                    # build
pio run -t upload          # flash via WCH-Link
```

It uses the same monotonic bit-clear strategy but operates on 32-bit RISC-V branch
instructions (BEQ) with three states instead of the ARM 16-bit Thumb encoding.

## Additional Ports

Standalone PlatformIO projects for other MCUs live alongside `targets/ch32v203/`
and follow the same `cd <target> && pio run` workflow:

| Path | MCU | Toolchain |
|---|---|---|
| `targets/stm32f4/` | STM32F411CE (Cortex-M4) | `platform=ststm32`, ST-Link |
| `targets/esp32c3/` | ESP32-C3 (RISC-V) | `platform=espressif32`, ESP-IDF |
| `targets/esp32s3/` | ESP32-S3 (Xtensa LX7) | `platform=espressif32`, ESP-IDF |

Each port reuses the shared `src/core/` and `src/compare/` sources and only
provides its own flash HAL, linker script, and console plumbing.

## Real-World LED Demo

The `realworld_freertos` target demonstrates live hotpatching on the nRF52840 DK:

1. After flashing, **LED1 blinks** at 10 ms intervals (the "victim" task).
2. Press **Button S1** to apply the MorphPatch at runtime.
3. **LED2 turns on** (patched path active) while **LED1 continues blinking** uninterrupted.
4. Press **Button S1** again to clear-forward unpatch the slot. **LED2 turns off** on the next victim tick, and a third patch attempt requires reflashing because NOR flash only supports monotonic 1-to-0 writes.

RTT output confirms the patch/unpatch cycle completed without stopping the victim task.

## Comparison Framework

The `compare` environment benchmarks four hotpatch techniques under identical conditions:

| Scheme | Mechanism | Trigger | Dispatch |
|---|---|---|---|
| **MorphPatch** | NOR flash bit-clear | Hardware (`B fun2` or exception on `BX R0`) | Inline branch or exception redirect |
| **RapidPatch** | Fixed patch point + bytecode VM | Software (function entry) | Linear scan |
| **HERA** | ARM FPB hardware breakpoint | Hardware (FPB remap) | RAM function pointer |
| **AutoPatch** | Static trampoline + binary search | Software (call site) | Binary search |

Run `compare` in the RTT console to produce a cycle-count comparison table across all
schemes for the selected CVE target.

## Hardware Requirements

- **ARM targets:** Nordic nRF52840 DK (PCA10056) + SEGGER J-Link
- **RISC-V target:** WCH CH32V203C8T6 board + WCH-Link

## License

See individual vendor directories for third-party license terms.
