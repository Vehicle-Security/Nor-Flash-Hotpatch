/*
 * bench_freertos_queue.h — FreeRTOS queue ping-pong benchmark.
 */
#ifndef BENCH_FREERTOS_QUEUE_H
#define BENCH_FREERTOS_QUEUE_H

#include <stdint.h>

/*
 * Run the queue ping-pong benchmark.
 * Performs xQueueSend + xQueueReceive in a tight loop.
 * iterations: number of send+receive pairs per run.
 * num_runs:   number of independent timed runs.
 * config_tag: "baseline", "full_instrumented", or "morphpatch"
 */
void bench_freertos_queue_run(uint32_t iterations, uint32_t num_runs,
                              const char *config_tag);

#endif /* BENCH_FREERTOS_QUEUE_H */
