# ClearBitPatch: NOR-Flash Hotpatching for Embedded Systems

ClearBitPatch is a runtime hotpatching technique for microcontrollers with NOR flash.
It exploits the monotonic 1-to-0 bit-clear property of NOR flash to redirect execution
from a vulnerable function to a patched replacement **without erasing any flash sector**
and **without halting the running firmware**.

## How It Works

A patch slot is pre-filled with `0xE7FF` (Thumb: `B .`, an infinite-loop opcode where
every data bit is 1).  At patch time, selected bits are cleared to form a 16-bit Thumb
branch instruction that jumps to the fix function.  Because only 1-to-0 transitions are
needed, the write completes in a single NOR flash word-program operation with no erase.

The patch includes write-verify-retry logic and a HardFault recovery handler that
redirects execution to the fix function even if the flash write fails.

## Repository Structure

```
core/             Core ClearBitPatch implementation (shared by all ARM targets)
ports/            RTOS port adapters (baremetal, FreeRTOS, LiteOS, NuttX, ...)
compare/          Benchmark comparison: ClearBitPatch vs RapidPatch / HERA / AutoPatch
realworld/        Real-world LED demo — live hotpatch without disrupting a blinking task
riscv/            RISC-V port (CH32V203C8T6, standalone PlatformIO project)
platform/         Hardware support (nRF52840 SDK wrappers, linker scripts, config)
vendor/           Third-party: nRF5 SDK 17.1.0, FreeRTOS-Kernel, LiteOS, NuttX, RapidPatch ref
tools/            J-Link scripts and RTT input files for automated testing
```

## Build Targets (ARM — nRF52840 DK)

All ARM targets are built from the root `platformio.ini`.  Install
[PlatformIO](https://platformio.org/) and run:

| Command | Description |
|---|---|
| `pio run -e baremetal` | ClearBitPatch on bare metal (baseline) |
| `pio run -e freertos` | ClearBitPatch on FreeRTOS |
| `pio run -e liteos` | ClearBitPatch on LiteOS |
| `pio run -e nuttx` | ClearBitPatch on NuttX |
| `pio run -e compare` | Benchmark all four hotpatch schemes |
| `pio run -e realworld` | LED demo, bare metal |
| `pio run -e realworld_freertos` | LED demo, FreeRTOS (press S1 to apply patch live) |

Upload to the board:

```bash
pio run -e <env> -t upload
```

Connect to the RTT console with **SEGGER RTT Viewer** (device: nRF52840_xxAA, SWD).

### RTT Console Commands

```
help        Show available commands
status      Print current patch state
patch       Apply the ClearBitPatch
unpatch     Remove the patch (clear-forward)
call        Execute the patch slot (vulnerable or fixed path)
demo        Run a full patch/unpatch/call cycle
bench       Measure cycle counts for patch operations
target <x>  Switch CVE target (cve2024-2212 | cve2025-1674 | cve2025-12899)
```

The `compare` environment adds:

```
mode <scheme>   Switch scheme: clearbitpatch | rapid | hera | autopatch
compare         Run benchmarks for all four schemes and print a comparison table
```

## Build Target (RISC-V — CH32V203C8T6)

The RISC-V port is a standalone PlatformIO project:

```bash
cd riscv
pio run                    # build
pio run -t upload          # flash via WCH-Link
```

It uses the same monotonic bit-clear strategy but operates on 32-bit RISC-V branch
instructions (BEQ) with three states instead of the ARM 16-bit Thumb encoding.

## Real-World LED Demo

The `realworld_freertos` target demonstrates live hotpatching on the nRF52840 DK:

1. After flashing, **LED1 blinks** at 250 ms intervals (the "victim" task).
2. Press **Button S1** to apply the ClearBitPatch at runtime.
3. **LED2 turns on** (patched path active) while **LED1 continues blinking** uninterrupted.

RTT output confirms the patch was applied without stopping the victim task.

## Comparison Framework

The `compare` environment benchmarks four hotpatch techniques under identical conditions:

| Scheme | Mechanism | Trigger | Dispatch |
|---|---|---|---|
| **ClearBitPatch** | NOR flash bit-clear | Hardware (flash read) | None (inline branch) |
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
