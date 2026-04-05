#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool available;
    bool apply_ok;
    bool baseline_ok;
    bool first_fix_ok;
    bool fix_ok;
    bool pure_patch_ok;
    bool unfix_ok;
    uint32_t patched_call_count;
    uint64_t t_base_cycles;
    uint64_t t_patch_cycles;
    uint64_t t_fix_first_cycles;
    uint64_t t_fix_cycles;
    uint64_t t_steady_cycles;
    uint64_t t_unfix_cycles;
    uint64_t t_roundtrip_cycles;
    int baseline_ret_code;
    int first_fix_ret_code;
    int fix_ret_code;
    int unfix_ret_code;
} patch_txn_benchmark_result_t;

patch_txn_benchmark_result_t benchmark_run(void);
void benchmark_print(const patch_txn_benchmark_result_t *result);
void benchmark_run_and_print(void);
bool benchmark_compare_run(patch_txn_benchmark_result_t *clearbit_result,
                           patch_txn_benchmark_result_t *erase_jump_result);
void benchmark_compare_run_and_print(void);

#endif
