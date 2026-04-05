#include "benchmark.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../app/app_mode.h"
#include "../console/console.h"
#include "../platform/cycle_counter.h"
#include "../patch/patch_control.h"
#include "../patch/patch_result.h"
#include "../target/cve_target.h"

#define BENCHMARK_PATCHED_CALLS 100u

static const char *yes_no(bool value) {
    return value ? "yes" : "no";
}

static void format_cycles(char *buf, size_t buf_size, uint32_t cycles) {
    if (cycles == 0xFFFFFFFFu) {
        (void)snprintf(buf, buf_size, "N/A");
        return;
    }

    (void)snprintf(buf, buf_size, "%lu", (unsigned long)cycles);
}

static void format_result(char *buf, size_t buf_size, int ret_code) {
    (void)snprintf(
        buf,
        buf_size,
        "%s(%d)",
        patch_result_name(ret_code),
        ret_code);
}

static uint32_t cycles_delta(uint32_t end_cycles, uint32_t start_cycles) {
    if (end_cycles == 0xFFFFFFFFu || start_cycles == 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    return end_cycles - start_cycles;
}

static uint32_t cycles_add(uint32_t lhs_cycles, uint32_t rhs_cycles) {
    uint64_t total_cycles = 0u;

    if (lhs_cycles == 0xFFFFFFFFu || rhs_cycles == 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }

    total_cycles = (uint64_t)lhs_cycles + (uint64_t)rhs_cycles;
    if (total_cycles > 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }

    return (uint32_t)total_cycles;
}

static bool benchmark_expectation_met(int ret_code, bool expect_fixed) {
    return expect_fixed ? patch_result_is_fixed(ret_code) : patch_result_is_vulnerable(ret_code);
}

patch_txn_benchmark_result_t benchmark_run(void) {
    patch_txn_benchmark_result_t result = {
        .available = false,
        .apply_ok = false,
        .baseline_ok = false,
        .first_fix_ok = false,
        .fix_ok = false,
        .pure_patch_ok = false,
        .unfix_ok = false,
        .patched_call_count = 0u,
        .t_base_cycles = 0xFFFFFFFFu,
        .t_patch_cycles = 0xFFFFFFFFu,
        .t_fix_first_cycles = 0xFFFFFFFFu,
        .t_fix_cycles = 0xFFFFFFFFu,
        .t_steady_cycles = 0xFFFFFFFFu,
        .t_unfix_cycles = 0xFFFFFFFFu,
        .t_roundtrip_cycles = 0xFFFFFFFFu,
        .baseline_ret_code = -999,
        .first_fix_ret_code = -999,
        .fix_ret_code = -999,
        .unfix_ret_code = -999,
    };
    bool all_fixed = true;
    uint32_t first_fix_end = 0xFFFFFFFFu;
    uint32_t fix_end = 0xFFFFFFFFu;
    uint32_t unapply_cycles = 0xFFFFFFFFu;
    uint32_t unfix_end = 0xFFFFFFFFu;
    bool patch_needs_cleanup = false;

    if (!patch_demo_can_run()) {
        console_printf(
            "[-] %s benchmark requires a pristine flash image. Reflash/reset before rerunning it.\r\n",
            patch_scheme_name());
        return result;
    }

    app_set_exec_mode(APP_EXEC_MODE_BENCHMARK);

    if (!cycle_counter_reset()) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }

    result.baseline_ret_code = patch_call();
    result.t_base_cycles = cycle_counter_read();
    result.baseline_ok = benchmark_expectation_met(result.baseline_ret_code, false);

    if (!result.baseline_ok) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        console_printf(
            "[-] %s baseline is not vulnerable under the unified attack input, ret=%d.\r\n",
            patch_scheme_name(),
            result.baseline_ret_code);
        return result;
    }

    if (!cycle_counter_reset()) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }

    result.apply_ok = patch_apply();
    patch_needs_cleanup = result.apply_ok;
    for (uint32_t i = 0; i < BENCHMARK_PATCHED_CALLS; ++i) {
        int ret_code = patch_call();

        if (i == 0u) {
            result.first_fix_ret_code = ret_code;
            first_fix_end = cycle_counter_read();
        }

        result.fix_ret_code = ret_code;
        result.patched_call_count++;
        if (!patch_result_is_fixed(ret_code)) {
            all_fixed = false;
            break;
        }
    }
    fix_end = cycle_counter_read();

    result.t_fix_first_cycles = first_fix_end;
    result.t_fix_cycles = fix_end;
    result.t_steady_cycles = cycles_delta(fix_end, first_fix_end);
    result.first_fix_ok = result.apply_ok && patch_result_is_fixed(result.first_fix_ret_code);
    result.fix_ok = result.apply_ok
        && all_fixed
        && (result.patched_call_count == BENCHMARK_PATCHED_CALLS);

    if (!cycle_counter_reset()) {
        if (patch_needs_cleanup) {
            patch_unapply();
        }
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }

    if (patch_needs_cleanup) {
        patch_unapply();
        patch_needs_cleanup = false;
        unapply_cycles = cycle_counter_read();
    }

    result.unfix_ret_code = patch_call();
    unfix_end = cycle_counter_read();

    if (unapply_cycles != 0xFFFFFFFFu) {
        result.t_unfix_cycles = unfix_end;
        result.t_roundtrip_cycles = cycles_add(result.t_fix_cycles, unapply_cycles);
    }
    result.unfix_ok = patch_result_is_vulnerable(result.unfix_ret_code);
    result.available = true;

    app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
    return result;
}

void benchmark_print(const patch_txn_benchmark_result_t *result) {
    char baseline_buf[24];
    char first_fix_buf[24];
    char fix_buf[24];
    char unfix_buf[24];
    char baseline_cycles[16];
    char first_fix_cycles[16];
    char fix_cycles[16];
    char steady_cycles[16];
    char unfix_cycles[16];
    char roundtrip_cycles[16];

    if (result == NULL) {
        return;
    }

    format_result(baseline_buf, sizeof(baseline_buf), result->baseline_ret_code);
    format_result(first_fix_buf, sizeof(first_fix_buf), result->first_fix_ret_code);
    format_result(fix_buf, sizeof(fix_buf), result->fix_ret_code);
    format_result(unfix_buf, sizeof(unfix_buf), result->unfix_ret_code);
    format_cycles(baseline_cycles, sizeof(baseline_cycles), result->t_base_cycles);
    format_cycles(first_fix_cycles, sizeof(first_fix_cycles), result->t_fix_first_cycles);
    format_cycles(fix_cycles, sizeof(fix_cycles), result->t_fix_cycles);
    format_cycles(steady_cycles, sizeof(steady_cycles), result->t_steady_cycles);
    format_cycles(unfix_cycles, sizeof(unfix_cycles), result->t_unfix_cycles);
    format_cycles(roundtrip_cycles, sizeof(roundtrip_cycles), result->t_roundtrip_cycles);

    console_puts("\r\n=== Benchmark Stages ===\r\n");
    console_printf(
        "scheme=%s target=%s available=%s\r\n",
        patch_scheme_name(),
        cve_target_name(cve_target_get_current()),
        yes_no(result->available));
    console_printf(
        "baseline     ret=%-16s ok=%-3s cycles=%s\r\n",
        baseline_buf,
        yes_no(result->baseline_ok),
        baseline_cycles);
    console_printf(
        "apply+fix1   ret=%-16s ok=%-3s cycles=%s\r\n",
        first_fix_buf,
        yes_no(result->first_fix_ok),
        first_fix_cycles);
    console_printf(
        "patched_%03lu  ret=%-16s ok=%-3s total=%s steady=%s\r\n",
        (unsigned long)BENCHMARK_PATCHED_CALLS,
        fix_buf,
        yes_no(result->fix_ok),
        fix_cycles,
        steady_cycles);
    console_printf(
        "unpatch+call ret=%-16s ok=%-3s cycles=%s roundtrip=%s\r\n",
        unfix_buf,
        yes_no(result->unfix_ok),
        unfix_cycles,
        roundtrip_cycles);
    console_printf(
        "[note] patched_%lu includes the first patched call. steady is calls 2..%lu.\r\n",
        (unsigned long)BENCHMARK_PATCHED_CALLS,
        (unsigned long)BENCHMARK_PATCHED_CALLS);
}

void benchmark_run_and_print(void) {
    patch_txn_benchmark_result_t result = benchmark_run();

    console_printf(
        "\r\n=== Benchmark [%s / %s] ===\r\n",
        patch_scheme_name(),
        cve_target_name(cve_target_get_current()));
    benchmark_print(&result);
}
