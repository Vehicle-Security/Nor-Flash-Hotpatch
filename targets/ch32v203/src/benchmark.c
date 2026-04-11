#include "benchmark.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "app_mode.h"
#include "console.h"
#include "cve_target.h"
#include "cycle_counter.h"
#include "patch_control.h"
#include "patch_result.h"

#define BENCHMARK_PATCHED_CALLS 100u
#define CYCLES_NOT_AVAILABLE 0xFFFFFFFFFFFFFFFFull

static const char *yes_no(bool value)
{
    return value ? "yes" : "no";
}

static const char *benchmark_scheme_name(const patch_scheme_ops_t *scheme)
{
    if ((scheme == NULL) || (scheme->name == NULL) || (scheme->name[0] == '\0')) {
        return "UNKNOWN";
    }

    return scheme->name;
}

static void format_cycles(char *buf, size_t buf_size, uint64_t cycles)
{
    char digits[21];
    size_t digit_count = 0u;
    size_t out_idx = 0u;

    if ((buf == NULL) || (buf_size == 0u)) {
        return;
    }

    if (cycles == CYCLES_NOT_AVAILABLE) {
        (void)snprintf(buf, buf_size, "N/A");
        return;
    }

    if (cycles == 0u) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    while ((cycles != 0u) && (digit_count < sizeof(digits))) {
        digits[digit_count++] = (char)('0' + (cycles % 10u));
        cycles /= 10u;
    }

    while ((digit_count > 0u) && ((out_idx + 1u) < buf_size)) {
        buf[out_idx++] = digits[--digit_count];
    }

    buf[out_idx] = '\0';
}

static void format_result(char *buf, size_t buf_size, int ret_code)
{
    (void)snprintf(buf, buf_size, "%s(%d)", patch_result_name(ret_code), ret_code);
}

static uint64_t cycles_delta(uint64_t end_cycles, uint64_t start_cycles)
{
    if ((end_cycles == CYCLES_NOT_AVAILABLE) || (start_cycles == CYCLES_NOT_AVAILABLE)) {
        return CYCLES_NOT_AVAILABLE;
    }
    return end_cycles - start_cycles;
}

static uint64_t cycles_add(uint64_t lhs_cycles, uint64_t rhs_cycles)
{
    if ((lhs_cycles == CYCLES_NOT_AVAILABLE) || (rhs_cycles == CYCLES_NOT_AVAILABLE)) {
        return CYCLES_NOT_AVAILABLE;
    }

    return lhs_cycles + rhs_cycles;
}

static void format_cycles_delta(char *buf, size_t buf_size, uint64_t base_cycles, uint64_t new_cycles)
{
    unsigned long long delta = 0ull;

    if ((buf == NULL) || (buf_size == 0u)) {
        return;
    }

    if ((base_cycles == CYCLES_NOT_AVAILABLE) || (new_cycles == CYCLES_NOT_AVAILABLE)) {
        (void)snprintf(buf, buf_size, "N/A");
        return;
    }

    if (new_cycles >= base_cycles) {
        delta = (unsigned long long)(new_cycles - base_cycles);
        (void)snprintf(buf, buf_size, "+%llu", delta);
    } else {
        delta = (unsigned long long)(base_cycles - new_cycles);
        (void)snprintf(buf, buf_size, "-%llu", delta);
    }
}

static bool benchmark_expectation_met(int ret_code, bool expect_fixed)
{
    return expect_fixed ? patch_result_is_fixed(ret_code) : patch_result_is_vulnerable(ret_code);
}

static patch_txn_benchmark_result_t benchmark_run_scheme(const patch_scheme_ops_t *scheme)
{
    patch_txn_benchmark_result_t result = {
        .available = false,
        .apply_ok = false,
        .baseline_ok = false,
        .first_fix_ok = false,
        .fix_ok = false,
        .pure_patch_ok = false,
        .unfix_ok = false,
        .patched_call_count = 0u,
        .t_base_cycles = CYCLES_NOT_AVAILABLE,
        .t_patch_cycles = CYCLES_NOT_AVAILABLE,
        .t_fix_first_cycles = CYCLES_NOT_AVAILABLE,
        .t_fix_cycles = CYCLES_NOT_AVAILABLE,
        .t_steady_cycles = CYCLES_NOT_AVAILABLE,
        .t_unfix_cycles = CYCLES_NOT_AVAILABLE,
        .t_roundtrip_cycles = CYCLES_NOT_AVAILABLE,
        .baseline_ret_code = -999,
        .first_fix_ret_code = -999,
        .fix_ret_code = -999,
        .unfix_ret_code = -999,
    };
    bool all_fixed = true;
    uint64_t first_fix_end = CYCLES_NOT_AVAILABLE;
    uint64_t fix_end = CYCLES_NOT_AVAILABLE;
    uint64_t unapply_cycles = CYCLES_NOT_AVAILABLE;
    uint64_t unfix_end = CYCLES_NOT_AVAILABLE;
    bool patch_needs_cleanup = false;
    uint32_t i;
    const char *scheme_name = benchmark_scheme_name(scheme);

    if ((scheme == NULL) || (scheme->call == NULL) || (scheme->apply == NULL) ||
        (scheme->unapply == NULL) || (scheme->demo_can_run == NULL)) {
        return result;
    }

    if (!scheme->demo_can_run()) {
        console_printf(
            "[-] %s benchmark requires a pristine flash image. Reset/reflash before rerunning.\r\n",
            scheme_name);
        return result;
    }

    app_set_exec_mode(APP_EXEC_MODE_BENCHMARK);

    if (!cycle_counter_reset()) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }

    result.baseline_ret_code = scheme->call();
    result.t_base_cycles = cycle_counter_read();
    result.baseline_ok = benchmark_expectation_met(result.baseline_ret_code, false);

    if (!result.baseline_ok) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        console_printf(
            "[-] %s baseline is not vulnerable under unified attack input, ret=%d.\r\n",
            scheme_name,
            result.baseline_ret_code);
        return result;
    }

    if (!cycle_counter_reset()) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }

    result.apply_ok = scheme->apply();
    patch_needs_cleanup = result.apply_ok;

    result.t_patch_cycles = cycle_counter_read();
    result.pure_patch_ok = result.apply_ok;

    for (i = 0u; i < BENCHMARK_PATCHED_CALLS; ++i) {
        int ret_code = scheme->call();

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
    result.fix_ok = result.apply_ok && all_fixed && (result.patched_call_count == BENCHMARK_PATCHED_CALLS);

    if (!cycle_counter_reset()) {
        if (patch_needs_cleanup) {
            scheme->unapply();
        }
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }

    if (patch_needs_cleanup) {
        scheme->unapply();
        patch_needs_cleanup = false;
        unapply_cycles = cycle_counter_read();
    }

    result.unfix_ret_code = scheme->call();
    unfix_end = cycle_counter_read();

    if (unapply_cycles != CYCLES_NOT_AVAILABLE) {
        result.t_unfix_cycles = unfix_end;
        result.t_roundtrip_cycles = cycles_add(result.t_fix_cycles, unapply_cycles);
    }
    result.unfix_ok = patch_result_is_vulnerable(result.unfix_ret_code);
    result.available = true;

    app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
    return result;
}

patch_txn_benchmark_result_t benchmark_run(void)
{
    return benchmark_run_scheme(patch_scheme_default());
}

static void benchmark_print_scheme(const patch_scheme_ops_t *scheme,
                                   const patch_txn_benchmark_result_t *result)
{
    char baseline_buf[24];
    char first_fix_buf[24];
    char fix_buf[24];
    char unfix_buf[24];
    char baseline_cycles[24];
    char patch_cycles[24];
    char first_fix_cycles[24];
    char fix_cycles[24];
    char steady_cycles[24];
    char unfix_cycles[24];
    char roundtrip_cycles[24];

    if (result == NULL) {
        return;
    }

    format_result(baseline_buf, sizeof(baseline_buf), result->baseline_ret_code);
    format_result(first_fix_buf, sizeof(first_fix_buf), result->first_fix_ret_code);
    format_result(fix_buf, sizeof(fix_buf), result->fix_ret_code);
    format_result(unfix_buf, sizeof(unfix_buf), result->unfix_ret_code);
    format_cycles(baseline_cycles, sizeof(baseline_cycles), result->t_base_cycles);
    format_cycles(patch_cycles, sizeof(patch_cycles), result->t_patch_cycles);
    format_cycles(first_fix_cycles, sizeof(first_fix_cycles), result->t_fix_first_cycles);
    format_cycles(fix_cycles, sizeof(fix_cycles), result->t_fix_cycles);
    format_cycles(steady_cycles, sizeof(steady_cycles), result->t_steady_cycles);
    format_cycles(unfix_cycles, sizeof(unfix_cycles), result->t_unfix_cycles);
    format_cycles(roundtrip_cycles, sizeof(roundtrip_cycles), result->t_roundtrip_cycles);

    console_puts("\r\n=== Benchmark Stages ===\r\n");
    console_printf(
        "scheme=%s target=%s available=%s\r\n",
        benchmark_scheme_name(scheme),
        cve_target_name(cve_target_get_current()),
        yes_no(result->available));
    console_printf(
        "baseline       ret=%-16s ok=%-3s cycles=%s\r\n",
        baseline_buf,
        yes_no(result->baseline_ok),
        baseline_cycles);
    console_printf(
        "patch_only     ok=%-3s cycles=%s\r\n",
        yes_no(result->pure_patch_ok),
        patch_cycles);
    console_printf(
        "apply+fix1     ret=%-16s ok=%-3s cycles=%s\r\n",
        first_fix_buf,
        yes_no(result->first_fix_ok),
        first_fix_cycles);
    console_printf(
        "patched_%03lu    ret=%-16s ok=%-3s total=%s steady=%s\r\n",
        (unsigned long)BENCHMARK_PATCHED_CALLS,
        fix_buf,
        yes_no(result->fix_ok),
        fix_cycles,
        steady_cycles);
    console_printf(
        "unpatch+call   ret=%-16s ok=%-3s cycles=%s roundtrip=%s\r\n",
        unfix_buf,
        yes_no(result->unfix_ok),
        unfix_cycles,
        roundtrip_cycles);
    console_printf(
        "[note] patched_%lu includes first patched call. steady is calls 2..%lu.\r\n",
        (unsigned long)BENCHMARK_PATCHED_CALLS,
        (unsigned long)BENCHMARK_PATCHED_CALLS);
}

void benchmark_print(const patch_txn_benchmark_result_t *result)
{
    benchmark_print_scheme(patch_scheme_default(), result);
}

void benchmark_run_and_print(void)
{
    patch_txn_benchmark_result_t result = benchmark_run();

    console_printf(
        "\r\n=== Benchmark [%s / %s] ===\r\n",
        benchmark_scheme_name(patch_scheme_default()),
        cve_target_name(cve_target_get_current()));
    benchmark_print_scheme(patch_scheme_default(), &result);
}

static void benchmark_print_compare_row(const char *label,
                                        uint64_t morph_cycles,
                                        uint64_t erase_rewrite_cycles)
{
    char morph_buf[24];
    char erase_rewrite_buf[24];
    char delta_buf[24];

    format_cycles(morph_buf, sizeof(morph_buf), morph_cycles);
    format_cycles(erase_rewrite_buf, sizeof(erase_rewrite_buf), erase_rewrite_cycles);
    format_cycles_delta(delta_buf, sizeof(delta_buf), morph_cycles, erase_rewrite_cycles);

    console_printf(
        "%-14s morph=%-15s erase-rewr=%-12s delta=%s\r\n",
        label,
        morph_buf,
        erase_rewrite_buf,
        delta_buf);
}

bool benchmark_compare_run(patch_txn_benchmark_result_t *morph_result,
                           patch_txn_benchmark_result_t *erase_rewrite_result)
{
    const patch_scheme_ops_t *morph_scheme = patch_scheme_default();
    const patch_scheme_ops_t *erase_rewrite_scheme = patch_scheme_cve2024_2212_direct();

    if ((morph_result == NULL) || (erase_rewrite_result == NULL)) {
        return false;
    }

    if (cve_target_get_current() != CVE_TARGET_CVE2024_2212) {
        return false;
    }

    if ((morph_scheme == NULL) || (erase_rewrite_scheme == NULL) ||
        (morph_scheme->demo_can_run == NULL) || (erase_rewrite_scheme->demo_can_run == NULL)) {
        return false;
    }

    if (!morph_scheme->demo_can_run() || !erase_rewrite_scheme->demo_can_run()) {
        return false;
    }

    *erase_rewrite_result = benchmark_run_scheme(erase_rewrite_scheme);
    *morph_result = benchmark_run_scheme(morph_scheme);
    return true;
}

void benchmark_compare_run_and_print(void)
{
    const patch_scheme_ops_t *morph_scheme = patch_scheme_default();
    const patch_scheme_ops_t *erase_rewrite_scheme = patch_scheme_cve2024_2212_direct();
    patch_txn_benchmark_result_t morph_result;
    patch_txn_benchmark_result_t erase_rewrite_result;

    if (cve_target_get_current() != CVE_TARGET_CVE2024_2212) {
        console_puts("[-] benchcmp only supports CVE2024-2212.\r\n");
        return;
    }

    if (!benchmark_compare_run(&morph_result, &erase_rewrite_result)) {
        console_puts("[-] benchcmp requires both scheme slots to be pristine. Reset/reflash before rerunning.\r\n");
        return;
    }

    console_printf(
        "\r\n=== Benchmark Compare [%s] ===\r\n",
        cve_target_name(cve_target_get_current()));
    console_printf("[scheme-a] %s\r\n", benchmark_scheme_name(morph_scheme));
    benchmark_print_scheme(morph_scheme, &morph_result);
    console_printf("[scheme-b] %s\r\n", benchmark_scheme_name(erase_rewrite_scheme));
    benchmark_print_scheme(erase_rewrite_scheme, &erase_rewrite_result);

    console_puts("\r\n=== Compare Summary ===\r\n");
    console_puts("[delta] erase-rewrite - morph\r\n");
    benchmark_print_compare_row("baseline", morph_result.t_base_cycles, erase_rewrite_result.t_base_cycles);
    benchmark_print_compare_row("patch_only", morph_result.t_patch_cycles, erase_rewrite_result.t_patch_cycles);
    benchmark_print_compare_row("apply+fix1", morph_result.t_fix_first_cycles, erase_rewrite_result.t_fix_first_cycles);
    benchmark_print_compare_row("patched_100", morph_result.t_fix_cycles, erase_rewrite_result.t_fix_cycles);
    benchmark_print_compare_row("steady", morph_result.t_steady_cycles, erase_rewrite_result.t_steady_cycles);
    benchmark_print_compare_row("unpatch+call", morph_result.t_unfix_cycles, erase_rewrite_result.t_unfix_cycles);
    benchmark_print_compare_row("roundtrip", morph_result.t_roundtrip_cycles, erase_rewrite_result.t_roundtrip_cycles);
}
