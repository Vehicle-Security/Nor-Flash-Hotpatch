/*
 * overhead_bench_main.c — FreeRTOS overhead benchmark entry point.
 *
 * Creates a single high-priority task that runs the queue ping-pong
 * benchmark at multiple iteration counts, then prints CSV results.
 *
 * Build as:
 *   pio run -e overhead_freertos_baseline
 *   pio run -e overhead_freertos_inst
 *   pio run -e overhead_freertos_morph
 */
#include "overhead_bench.h"
#include "bench_freertos_queue.h"

#include "FreeRTOS.h"
#include "task.h"

#include "SEGGER_RTT.h"
#include "boards.h"
#include "nrf.h"

/* ------------------------------------------------------------------ */
/* Config tag — set by build define, defaults to "baseline"           */
/* ------------------------------------------------------------------ */
#ifndef BENCH_CONFIG_TAG
#define BENCH_CONFIG_TAG "baseline"
#endif

#define BENCH_RUNS  30u

/* ------------------------------------------------------------------ */
/* Benchmark task                                                     */
/* ------------------------------------------------------------------ */

static void bench_task(void *pvParameters) {
    (void)pvParameters;

    /* Wait 3 seconds for RTT Viewer to connect. */
    SEGGER_RTT_printf(0, "\r\n[WAIT] Starting in 3s, open RTT Viewer now...\r\n");
    vTaskDelay(pdMS_TO_TICKS(3000));

    SEGGER_RTT_printf(0, "\r\n========================================\r\n");
    SEGGER_RTT_printf(0, "  Overhead Benchmark — FreeRTOS\r\n");
    SEGGER_RTT_printf(0, "  Config: %s\r\n", BENCH_CONFIG_TAG);
    SEGGER_RTT_printf(0, "  CPU: %lu Hz\r\n", (unsigned long)BENCH_CPU_HZ);
    SEGGER_RTT_printf(0, "========================================\r\n\r\n");

    /* Init DWT. */
    if (!benchmark_timer_init()) {
        SEGGER_RTT_printf(0, "[ERROR] DWT CYCCNT not available!\r\n");
        for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    SEGGER_RTT_printf(0, "[INFO] DWT CYCCNT initialized.\r\n");

    /* Print CSV header. */
    bench_print_csv_header();

    /* Run queue ping-pong: 10^5 iterations, 30 runs. */
    SEGGER_RTT_printf(0, "\r\n--- Queue Ping-Pong N=100000 ---\r\n");
    bench_freertos_queue_run(100000u, BENCH_RUNS, BENCH_CONFIG_TAG);

    SEGGER_RTT_printf(0, "\r\n[DONE] All benchmarks complete.\r\n");

    /* Idle forever. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    /* Minimal board init (LEDs, clocks). */
    bsp_board_init(BSP_INIT_LEDS);

    /* RTT init. */
    SEGGER_RTT_Init();
    SEGGER_RTT_printf(0, "\r\n[BOOT] Overhead Bench FreeRTOS (%s)\r\n", BENCH_CONFIG_TAG);

    /* Create benchmark task at highest app priority. */
    BaseType_t ret = xTaskCreate(
        bench_task,
        "bench",
        1024,           /* stack words */
        NULL,
        configMAX_PRIORITIES - 1,
        NULL
    );
    configASSERT(ret == pdPASS);

    /* Start scheduler — does not return. */
    vTaskStartScheduler();

    /* Should never reach here. */
    for (;;) {}
    return 0;
}

/* FreeRTOS memory callbacks (required when configSUPPORT_STATIC_ALLOCATION=0). */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    (void)pcTaskName;
    SEGGER_RTT_printf(0, "[FATAL] Stack overflow in %s\r\n", pcTaskName);
    for (;;) {}
}
