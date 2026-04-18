/*
 * overhead_bench.h — Shared DWT benchmark harness for RTOS overhead comparison.
 *
 * Provides:
 *   - DWT CYCCNT initialization and measurement
 *   - Statistical summary (min, max, mean, median, stddev)
 *   - RTT output formatting
 *   - Iteration-count control
 */
#ifndef OVERHEAD_BENCH_H
#define OVERHEAD_BENCH_H

#include <stdbool.h>
#include <stdint.h>

/* ---------- configuration ---------- */
#define BENCH_MAX_RUNS        50u
#define BENCH_DEFAULT_RUNS    30u
#define BENCH_CPU_HZ          64000000u   /* nRF52840 = 64 MHz */

/* ---------- statistics ---------- */
typedef struct {
    uint32_t min;
    uint32_t max;
    uint32_t mean;
    uint32_t median;
    uint32_t stddev;
    uint32_t total_cycles;
    uint32_t avg_per_iter;
    uint32_t num_runs;
    uint32_t iterations;
} bench_stats_t;

/* ---------- result row (one config x one iteration count) ---------- */
typedef struct {
    const char *rtos_name;
    const char *bench_name;
    const char *config_name;        /* "baseline", "full_instrumented", "morphpatch" */
    uint32_t iterations;
    uint32_t raw_cycles[BENCH_MAX_RUNS];
    uint32_t num_runs;
    bench_stats_t stats;
} bench_result_t;

/* ---------- DWT timer API ---------- */
bool     benchmark_timer_init(void);
void     benchmark_timer_start(void);
uint32_t benchmark_timer_stop(void);
uint32_t benchmark_timer_read(void);

/* ---------- statistics API ---------- */
void bench_compute_stats(bench_result_t *result);
void bench_print_stats(const bench_result_t *result);
void bench_print_csv_header(void);
void bench_print_csv_row(const bench_result_t *result);

/* ---------- global instrumentation sinks ---------- */
/* RapidPatch-style: conditional check (load + branch + increment) */
extern volatile uint32_t __rapid_patch_active;
extern volatile uint32_t __rapid_dispatch_sink;
/* AutoPatch-style: trampoline pointer check (load + NULL test) */
extern volatile void    *__auto_trampoline_ptr;
extern volatile uint32_t __auto_dispatch_sink;

#endif /* OVERHEAD_BENCH_H */
