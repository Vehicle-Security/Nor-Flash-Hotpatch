/*
 * main_benchcmp.c — nRF52840 benchcmp entry point.
 *
 * Runs the MorphPatch vs EraseRewrite comparison benchmark on startup,
 * then enters the normal interactive shell.
 */
#include <stddef.h>
#include <string.h>

#include "bsp.h"

#include "core/benchmark/benchmark_compare.h"
#include "core/console/console.h"
#include "core/patch/morph_patch_erase.h"
#include "core/platform/cycle_counter.h"
#include "core/target/cve_target.h"

void console_handle_command_benchcmp(const char *cmd)
{
    if (strcmp(cmd, "benchcmp") == 0) {
        benchmark_compare_run_and_print();
        return;
    }

    if (strcmp(cmd, "erasestatus") == 0) {
        erase_patch_print_status();
        return;
    }

    /* Fall through to normal handler */
    console_handle_command(cmd);
}

int main(void)
{
    char cmd[48];
    size_t len = 0u;

    bsp_board_init(BSP_INIT_LEDS);
    console_init();

    if (cycle_counter_init()) {
        console_puts("[init] DWT cycle counter ready.\r\n");
    } else {
        console_puts("[init] Warning: DWT cycle counter unavailable.\r\n");
    }

    console_puts("commands: help, bench, benchcmp, target, demo, call, patch, unpatch, status, path, erasestatus\r\n");
    console_puts("target names: cve2024-2212 | cve2025-1674 | cve2025-12899\r\n");
    print_patch_status();
    erase_patch_print_status();
    console_prompt();

    while (1) {
        if (!console_poll_line(cmd, sizeof(cmd), &len)) {
            continue;
        }

        console_handle_command_benchcmp(cmd);
        len = 0u;
        console_prompt();
    }
}
