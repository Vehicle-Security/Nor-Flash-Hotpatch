/*
 * main.c -- ESP32-C3 MorphPatch demo entry point.
 *
 * ESP-IDF uses app_main() as the entry point (called from the default
 * FreeRTOS main task).  The interactive shell loop yields to the
 * scheduler via vTaskDelay when no input is available.
 */
#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "benchmark.h"
#include "console.h"
#include "cve_target.h"
#include "cycle_counter.h"

#if defined(APP_AUTOBENCH_TARGET)
volatile patch_txn_benchmark_result_t g_autobench_result;
volatile uint32_t g_autobench_done = 0u;
volatile uint32_t g_autobench_target = 0u;
volatile patch_txn_benchmark_result_t g_autobench_compare_morph_result;
volatile patch_txn_benchmark_result_t g_autobench_compare_erase_result;
volatile uint32_t g_autobench_compare_done = 0u;
#endif

void app_main(void)
{
    char cmd[80];
    size_t len = 0u;

    console_init();

    if (cycle_counter_init()) {
        console_puts("[init] cycle counter ready.\r\n");
    } else {
        console_puts("[init] Warning: cycle counter unavailable.\r\n");
    }

#if defined(APP_AUTOBENCH_TARGET)
    cve_target_set_current((cve_target_t)(APP_AUTOBENCH_TARGET));
    g_autobench_target = (uint32_t)cve_target_get_current();
#if defined(APP_AUTOBENCH_COMPARE)
    if (benchmark_compare_run((patch_txn_benchmark_result_t *)&g_autobench_compare_morph_result,
                              (patch_txn_benchmark_result_t *)&g_autobench_compare_erase_result)) {
        g_autobench_compare_done = 1u;
    }
#else
    g_autobench_result = benchmark_run();
    g_autobench_done = 1u;
#endif
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif

    console_print_help();
    console_print_status();
    console_run_startup_smoke_test();
    console_prompt();

    while (true) {
        if (!console_poll_line(cmd, sizeof(cmd), &len)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        console_handle_command(cmd);
        len = 0u;
        console_prompt();
    }
}
