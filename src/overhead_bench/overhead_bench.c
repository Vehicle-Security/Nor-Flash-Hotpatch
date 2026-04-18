/*
 * overhead_bench.c — Shared DWT benchmark harness implementation.
 */
#include "overhead_bench.h"

#include <string.h>

#include "nrf.h"
#include "SEGGER_RTT.h"

/* Global instrumentation sinks.
 * RapidPatch stub: if (__rapid_patch_active) { __rapid_dispatch_sink++; }
 *   → models eBPF VM dispatcher "no-match" fast path.
 *   __rapid_patch_active = 0 so the branch is NOT taken, but the
 *   volatile load + compare + conditional branch still executes.
 *
 * AutoPatch stub: if (__auto_trampoline_ptr) { __auto_dispatch_sink++; }
 *   → models static trampoline "no handler installed" fast path.
 *   __auto_trampoline_ptr = NULL so the branch is NOT taken.
 */
volatile uint32_t __rapid_patch_active  = 0u;
volatile uint32_t __rapid_dispatch_sink = 0u;
volatile void    *__auto_trampoline_ptr = (void *)0;
volatile uint32_t __auto_dispatch_sink  = 0u;

/* ------------------------------------------------------------------ */
/* DWT timer                                                          */
/* ------------------------------------------------------------------ */

bool benchmark_timer_init(void) {
#if defined(DWT) && defined(CoreDebug)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

#ifdef DWT_CTRL_NOCYCCNT_Msk
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0u) {
        return false;
    }
#endif

    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    return ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u);
#else
    return false;
#endif
}

void benchmark_timer_start(void) {
    __DSB();
    __ISB();
    DWT->CYCCNT = 0u;
    __DSB();
    __ISB();
}

uint32_t benchmark_timer_stop(void) {
    __DSB();
    __ISB();
    return DWT->CYCCNT;
}

uint32_t benchmark_timer_read(void) {
    return DWT->CYCCNT;
}

/* ------------------------------------------------------------------ */
/* Statistics                                                         */
/* ------------------------------------------------------------------ */

static void sort_u32(uint32_t *arr, uint32_t n) {
    /* Simple insertion sort — n is small (<=50). */
    for (uint32_t i = 1; i < n; i++) {
        uint32_t key = arr[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

static uint32_t isqrt_u64(uint64_t n) {
    if (n == 0u) return 0u;
    uint64_t x = n;
    uint64_t y = (x + 1u) / 2u;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2u;
    }
    return (uint32_t)x;
}

void bench_compute_stats(bench_result_t *result) {
    uint32_t sorted[BENCH_MAX_RUNS];
    uint32_t n = result->num_runs;
    uint64_t sum = 0u;
    uint64_t sum_sq = 0u;

    if (n == 0u) {
        memset(&result->stats, 0, sizeof(result->stats));
        return;
    }

    memcpy(sorted, result->raw_cycles, n * sizeof(uint32_t));
    sort_u32(sorted, n);

    result->stats.min = sorted[0];
    result->stats.max = sorted[n - 1u];
    result->stats.median = sorted[n / 2u];

    for (uint32_t i = 0; i < n; i++) {
        sum += sorted[i];
    }
    result->stats.mean = (uint32_t)(sum / n);
    result->stats.total_cycles = (uint32_t)sum;  /* may overflow for very large values */

    for (uint32_t i = 0; i < n; i++) {
        int64_t diff = (int64_t)sorted[i] - (int64_t)result->stats.mean;
        sum_sq += (uint64_t)(diff * diff);
    }
    result->stats.stddev = isqrt_u64(sum_sq / n);

    result->stats.num_runs = n;
    result->stats.iterations = result->iterations;
    result->stats.avg_per_iter = (result->iterations > 0u)
        ? (uint32_t)(sum / (uint64_t)result->iterations / (uint64_t)n)
        : 0u;
}

void bench_print_stats(const bench_result_t *result) {
    const bench_stats_t *s = &result->stats;
    uint32_t time_us = (uint32_t)((uint64_t)s->mean * 1000000u / BENCH_CPU_HZ);

    SEGGER_RTT_printf(0,
        "\r\n[BENCH] rtos=%s bench=%s config=%s iter=%lu\r\n",
        result->rtos_name, result->bench_name, result->config_name,
        (unsigned long)result->iterations);

    SEGGER_RTT_printf(0,
        "[BENCH] runs=%lu min=%lu max=%lu mean=%lu median=%lu stddev=%lu\r\n",
        (unsigned long)s->num_runs,
        (unsigned long)s->min,
        (unsigned long)s->max,
        (unsigned long)s->mean,
        (unsigned long)s->median,
        (unsigned long)s->stddev);

    SEGGER_RTT_printf(0,
        "[BENCH] total=%lu avg_per_iter=%lu time_us=%lu\r\n",
        (unsigned long)s->total_cycles,
        (unsigned long)s->avg_per_iter,
        (unsigned long)time_us);
}

void bench_print_csv_header(void) {
    SEGGER_RTT_printf(0,
        "[CSV] rtos,benchmark,config,iterations,runs,min,max,mean,median,stddev,total,avg_per_iter,time_us\r\n");
}

void bench_print_csv_row(const bench_result_t *result) {
    const bench_stats_t *s = &result->stats;
    uint32_t time_us = (uint32_t)((uint64_t)s->mean * 1000000u / BENCH_CPU_HZ);

    SEGGER_RTT_printf(0,
        "[CSV] %s,%s,%s,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\r\n",
        result->rtos_name, result->bench_name, result->config_name,
        (unsigned long)result->iterations,
        (unsigned long)s->num_runs,
        (unsigned long)s->min,
        (unsigned long)s->max,
        (unsigned long)s->mean,
        (unsigned long)s->median,
        (unsigned long)s->stddev,
        (unsigned long)s->total_cycles,
        (unsigned long)s->avg_per_iter,
        (unsigned long)time_us);
}
