/*
 * bench_freertos_queue.c — FreeRTOS queue ping-pong overhead benchmark.
 *
 * Measures the DWT cycle cost of N iterations of:
 *   xQueueSend(q, &val, 0)  +  xQueueReceive(q, &buf, 0)
 *
 * Single-task, queue depth 1, no blocking, no context switch.
 * This isolates kernel API overhead from scheduler noise.
 *
 * The same binary runs under baseline / full_instrumented / morphpatch —
 * the difference is which kernel .c files were built with which stubs.
 */
#include "bench_freertos_queue.h"
#include "overhead_bench.h"

#include "FreeRTOS.h"
#include "queue.h"

#include "SEGGER_RTT.h"

/* Queue handle — created once, reused across runs. */
static QueueHandle_t s_bench_queue = NULL;

static void bench_queue_init(void) {
    if (s_bench_queue == NULL) {
        s_bench_queue = xQueueCreate(1, sizeof(uint32_t));
        configASSERT(s_bench_queue != NULL);
    } else {
        xQueueReset(s_bench_queue);
    }
}

/*
 * Core timed loop — must be noinline to prevent the compiler from
 * reordering DWT reads across the loop boundary.
 */
static __attribute__((noinline)) uint32_t bench_queue_pingpong(uint32_t iterations) {
    QueueHandle_t q = s_bench_queue;
    uint32_t val = 0xDEADBEEFu;
    uint32_t buf = 0u;
    volatile uint32_t sink = 0u;

    benchmark_timer_start();

    for (uint32_t i = 0; i < iterations; i++) {
        xQueueSend(q, &val, 0);
        xQueueReceive(q, &buf, 0);
    }

    sink = buf;     /* prevent dead-code elimination */
    (void)sink;
    return benchmark_timer_stop();
}

void bench_freertos_queue_run(uint32_t iterations, uint32_t num_runs,
                              const char *config_tag) {
    bench_result_t result;

    if (num_runs > BENCH_MAX_RUNS) {
        num_runs = BENCH_MAX_RUNS;
    }

    result.rtos_name   = "freertos";
    result.bench_name  = "queue_pingpong";
    result.config_name = config_tag;
    result.iterations  = iterations;
    result.num_runs    = num_runs;

    bench_queue_init();

    /* Warm up — one untimed run to populate caches / branch predictors. */
    (void)bench_queue_pingpong(iterations > 100u ? 100u : iterations);

    /* Timed runs. */
    for (uint32_t r = 0; r < num_runs; r++) {
        xQueueReset(s_bench_queue);
        result.raw_cycles[r] = bench_queue_pingpong(iterations);

        /* Print individual run (helpful for live RTT monitoring). */
        SEGGER_RTT_printf(0, "[BENCH] run=%lu cycles=%lu\r\n",
            (unsigned long)(r + 1u),
            (unsigned long)result.raw_cycles[r]);
    }

    bench_compute_stats(&result);
    bench_print_stats(&result);
    bench_print_csv_row(&result);
}
