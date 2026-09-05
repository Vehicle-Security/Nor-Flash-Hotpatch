# TransBit — NOR-Flash Hotpatching for Embedded Systems

TransBit is a runtime hotpatching technique for microcontrollers with NOR
flash. It exploits the monotonic **1-to-0** bit-clear property of NOR flash to
redirect execution from a vulnerable function to a patched replacement
**without erasing any flash sector** and **without halting the running
firmware**. The framework ships with ports for bare metal and five RTOSes,
five MCU targets across ARM Cortex-M, RISC-V and Xtensa, three real CVE
workloads, and a comparison harness that benchmarks TransBit against
RapidPatch, HERA and AutoPatch under identical conditions.

## How It Works

TransBit reserves a tiny factory slot at every protected function entry.
The slot starts as `0xE7FF` (a Thumb branch to the next instruction — a
1-cycle NOP that falls through to the original body). Two monotonic
dispatch paths can then be armed at runtime from that same slot:

- **Direct-branch path:** `0xE7FF → Thumb B(patch_fun) → 0xE000`
- **Fault-dispatch path:** `0xE7FF → 0x4700 → 0x4600`

The direct path rewrites the halfword to an in-range Thumb branch to the
replacement function. When the target is out of range or a bit cannot be
cleared monotonically, TransBit falls back to the fault-dispatch path:
`0x4700` (`BX R0`) triggers a controlled exception, the handler restores
the caller's R0 from the stacked R12 shadow, and redirects execution to
the fix function. Unpatch clears one more bit to `0x4600`, restoring the
original path without an erase cycle.

All state transitions only ever clear 1-bits to 0, so every transition is
legal on NOR flash and needs no sector erase.

### Runtime path selection

Switch the dispatch path from the RTT console before applying a patch:

- `path direct` — prefer the direct branch; fall back to fault-dispatch
  automatically if the direct write cannot be programmed monotonically.
- `path fault` — force the fault-dispatch path from the start.

## Repository Structure

```text
src/
  core/            TransBit core: patch dispatch, app, console, CVE targets
  compare/         Multi-scheme harness: TransBit vs RapidPatch / HERA / AutoPatch
  realworld/       LED demo — live hotpatch while a FreeRTOS task keeps blinking
  overhead_bench/  DWT cycle-count harness for RTOS instrumentation overhead
ports/
  baremetal/       Bare-metal entry and platform glue (nRF52840)
  freertos/        FreeRTOS port
  liteos/          Huawei LiteOS port
  nuttx/           Apache NuttX port
  rtthread/        RT-Thread port
  zephyr/          Zephyr RTOS port
  common/          Shared port headers
targets/
  nrf52840/        ARM Cortex-M4F: SDK wrappers, linker scripts, J-Link tooling
  ch32v203/        RISC-V (WCH CH32V203C8T6) standalone PlatformIO project
  stm32f4/         ARM Cortex-M4 (STM32F411CE) standalone PlatformIO project
  esp32c3/         RISC-V (ESP32-C3) ESP-IDF project
  esp32s3/         Xtensa LX7 (ESP32-S3) ESP-IDF project
  vendor/          Third-party: nRF5 SDK 17.1.0, FreeRTOS-Kernel, LiteOS, NuttX, RapidPatch
scripts/           Build/bench automation (Python + PowerShell)
docs/              Benchmark plan, research summary, final report
results/           Raw benchmark data and analysis tables
```

## Build Targets (nRF52840 DK)

All ARM targets build from the root `platformio.ini`. Install
[PlatformIO](https://platformio.org/) and run:

| Command | Description |
|---|---|
| `pio run -e baremetal` | TransBit on bare metal (baseline) |
| `pio run -e benchcmp` | TransBit vs erase-rewrite, bare metal |
| `pio run -e freertos` | TransBit on FreeRTOS |
| `pio run -e liteos` | TransBit on LiteOS |
| `pio run -e nuttx` | TransBit on NuttX |
| `pio run -e compare` | Benchmark all four hotpatch schemes |
| `pio run -e realworld` | LED demo, bare metal |
| `pio run -e realworld_freertos` | LED demo, FreeRTOS (S1 applies / re-press unpatches) |

Flash with `pio run -e <env> -t upload`. Open the RTT console with
**SEGGER RTT Viewer** (device `nRF52840_xxAA`, SWD).

### RTT Console Commands

```text
help          Show available commands
path          Show current TransBit path
path <x>      Switch path: direct | fault
status        Print current patch state
patch         Apply the TransBit
unpatch       Remove the patch (clear-forward)
call          Execute the patch slot (vulnerable or fixed path)
demo          Run a full patch / unpatch / call cycle
bench         Measure cycle counts for patch operations
target <x>    Switch CVE target: cve2024-2212 | cve2025-1674 | cve2025-12899
```

The `compare` environment adds:

```text
mode <scheme> Switch scheme: TransBit | rapid | hera | autopatch
compare       Run benchmarks for all four schemes and print a table
```

## Other Targets

Each non-nRF target is a standalone PlatformIO project and follows the same
`cd <target> && pio run` workflow, reusing `src/core/` and `src/compare/`.

| Path | MCU | Toolchain |
|---|---|---|
| `targets/ch32v203/` | WCH CH32V203C8T6 (RISC-V) | `platform=ch32v`, WCH-Link |
| `targets/stm32f4/` | STM32F411CE (Cortex-M4) | `platform=ststm32`, ST-Link |
| `targets/esp32c3/` | ESP32-C3 (RISC-V) | `platform=espressif32`, ESP-IDF |
| `targets/esp32s3/` | ESP32-S3 (Xtensa LX7) | `platform=espressif32`, ESP-IDF |

The RISC-V ports use the same monotonic bit-clear strategy but operate on
32-bit RISC-V branch instructions (`BEQ`) with three states instead of the
ARM 16-bit Thumb encoding.

## Real-time Task Demo

The `realworld_freertos` target demonstrates **online hotpatching under concurrent real-time execution** on the nRF52840 DK.

In this demo, a periodic victim task runs continuously with a **10 ms period**, while a separate patch-manager task installs and removes TransBit at runtime. Press **S1** once to apply the patch online, and press **S1** again to remove it. Throughout the entire process, the victim task continues executing normally, with **no reboot, no stop-the-world phase, and no visible stall** during patch installation, patched execution, or patch removal.

RTT logs confirm that the patch / unpatch cycle completes successfully **without suspending or halting the running task**. This demo is intended to show that TransBit supports **real-time hotpatching on a live system**, rather than merely switching between two code paths.

> Note: because the patch slot in NOR Flash only supports monotonic **1→0** state transitions, a third patch attempt requires reflashing.

## Comparison Framework

The `compare` environment benchmarks four hotpatch techniques under
identical conditions on the same CVE target.

| Scheme | Mechanism | Trigger | Dispatch |
|---|---|---|---|
| **TransBit** | NOR flash bit-clear | Hardware (`B fun2` or exception on `BX R0`) | Inline branch or exception redirect |
| **RapidPatch** | Fixed patch point + bytecode VM | Software (function entry) | Linear scan |
| **HERA** | ARM FPB hardware breakpoint | Hardware (FPB remap) | RAM function pointer |
| **AutoPatch** | Static trampoline + binary search | Software (call site) | Binary search |

Run `compare` in the RTT console to emit a cycle-count comparison table
across all schemes for the selected CVE target. Flash / RAM memory cost
is reported alongside in the `baremetal` and `compare` environments.

## RTOS Instrumentation Overhead Bench

`src/overhead_bench/` drives a DWT-based queue ping-pong benchmark that
measures the steady-state cost of the *no-match fast path* that RapidPatch
and AutoPatch must run at every kernel entry. Static analysis on the
FreeRTOS kernel (see `results/summary_results.md`):

| Config | .text delta | Avg. instructions / function | Est. cycles / call |
|---|---|---|---|
| Baseline | — | — | 0 |
| RapidPatch | +892 B (+8.0%) | +10.4 | 4–6 |
| AutoPatch | +884 B (+7.9%) | +10.4 | 4–6 |
| **TransBit** | **+180 B (+1.6%)** | **+1.8** | **2** |

The accompanying scripts under `scripts/` automate source instrumentation
(`instrument_all_functions.py`), static analysis (`analyze_overhead.py`),
and board runs (`run_overhead_bench.ps1`, `run_compare_matrix.ps1`).

## Documentation

- `docs/benchmark_plan.md` — methodology
- `docs/research_summary.md` — related work and positioning
- `docs/final_report.md` — full write-up of the overhead experiment
- `results/` — CSV/Markdown dumps of every benchmark run

## Hardware Requirements

- **nRF52840 DK (PCA10056)** + SEGGER J-Link — primary target
- **WCH CH32V203C8T6** + WCH-Link — RISC-V port
- **STM32F411CE** + ST-Link — STM32 port
- **ESP32-C3 / ESP32-S3 DevKit** — ESP-IDF ports

## License

See individual vendor directories for third-party license terms. Core
TransBit sources under `src/` and `ports/` are released for research and
educational use; consult each file header for specifics.
