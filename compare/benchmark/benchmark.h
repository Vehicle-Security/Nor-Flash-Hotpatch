#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdbool.h>
#include <stdint.h>

#include "compare/patch/patch_control.h"

typedef struct {
    bool available;
    bool install_ok;
    bool baseline_ok;
    bool pass_ok;
    bool first_fix_ok;
    bool steady_ok;
    bool uninstall_ok;
    bool trigger_is_approx;
    bool pass_is_approx;
    int baseline_ret_code;
    int benign_baseline_ret_code;
    int pass_ret_code;
    int first_fix_ret_code;
    int steady_last_ret_code;
    int uninstalled_ret_code;
    uint32_t t_background;
    uint32_t t_pass_only;
    uint32_t t_install;
    uint32_t t_trigger_only;
    uint32_t t_dispatch_only_1;
    uint32_t t_dispatch_only_5;
    uint32_t t_dispatch_only_64;
    uint32_t t_patch_exec_only;
    uint32_t t_first_fix;
    uint32_t t_steady_100;
    uint32_t t_uninstall;
    uint32_t t_roundtrip;
} patch_txn_benchmark_result_t;

patch_txn_benchmark_result_t benchmark_run_for_scheme(patch_scheme_t scheme);
void benchmark_print_single_scheme(patch_scheme_t scheme, const patch_txn_benchmark_result_t *result);
void benchmark_run_and_print(patch_scheme_t scheme);
void benchmark_compare_and_print(void);

#endif
