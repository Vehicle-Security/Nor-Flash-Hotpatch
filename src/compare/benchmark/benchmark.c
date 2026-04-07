#include "benchmark.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "SEGGER_RTT.h"

#include "compare/src/compare.h"
#include "core/app/app_mode.h"
#include "compare/console/console.h"
#include "core/platform/cycle_counter.h"
#include "core/patch/patch_result.h"
#include "core/target/cve_target.h"

#define BENCHMARK_STEADY_CALLS 100u

typedef struct {
    patch_scheme_t scheme;
    const char *table_name;
    scheme_ops_t ops;
    uint32_t (*bench_trigger_only)(void);
    uint32_t (*bench_dispatch_only)(uint32_t active_patch_count);
    uint32_t (*bench_patch_exec_only)(void);
    bool trigger_is_approx_when_missing;
    bool pass_is_approx_when_arg_ignored;
    memory_cost_t (*memory_cost)(void);
} benchmark_scheme_ops_t;

static const benchmark_scheme_ops_t g_bench_schemes[] = {
    {
        .scheme = PATCH_SCHEME_RAPID,
        .table_name = "RapidPatch-fixed",
        .ops = {
            .name = "rapid",
            .install = rapid_patch_install,
            .uninstall = rapid_patch_unapply,
            .is_active = rapid_patch_is_active,
            .invoke = rapid_invoke,
        },
        .bench_trigger_only = rapid_bench_trigger_only,
        .bench_dispatch_only = rapid_bench_dispatch_only,
        .bench_patch_exec_only = rapid_bench_patch_exec_only,
        .trigger_is_approx_when_missing = false,
        .pass_is_approx_when_arg_ignored = false,
        .memory_cost = rapid_memory_cost,
    },
    {
        .scheme = PATCH_SCHEME_HERA,
        .table_name = "HERA",
        .ops = {
            .name = "hera",
            .install = hera_patch_install,
            .uninstall = hera_patch_unapply,
            .is_active = hera_patch_is_active,
            .invoke = hera_invoke,
        },
        .bench_trigger_only = NULL,
        .bench_dispatch_only = NULL,
        .bench_patch_exec_only = NULL,
        .trigger_is_approx_when_missing = true,
        .pass_is_approx_when_arg_ignored = true,
        .memory_cost = hera_memory_cost,
    },
    {
        .scheme = PATCH_SCHEME_AUTOPATCH,
        .table_name = "AutoPatch-static",
        .ops = {
            .name = "autopatch",
            .install = autopatch_patch_install,
            .uninstall = autopatch_patch_unapply,
            .is_active = autopatch_patch_is_active,
            .invoke = autopatch_invoke,
        },
        .bench_trigger_only = autopatch_bench_trigger_only,
        .bench_dispatch_only = autopatch_bench_dispatch_only,
        .bench_patch_exec_only = autopatch_bench_patch_exec_only,
        .trigger_is_approx_when_missing = false,
        .pass_is_approx_when_arg_ignored = false,
        .memory_cost = autopatch_memory_cost,
    },
    {
        .scheme = PATCH_SCHEME_LEGACY,
        .table_name = "ClearBitPatch",
        .ops = {
            .name = "clearbit",
            .install = clearbit_patch_install,
            .uninstall = clearbit_patch_unapply,
            .is_active = clearbit_patch_is_active,
            .invoke = clearbit_invoke,
        },
        .bench_trigger_only = NULL,
        .bench_dispatch_only = NULL,
        .bench_patch_exec_only = NULL,
        .trigger_is_approx_when_missing = true,
        .pass_is_approx_when_arg_ignored = true,
        .memory_cost = clearbit_memory_cost,
    },
};

static const benchmark_scheme_ops_t *find_bench_scheme(patch_scheme_t scheme) {
    for (size_t i = 0; i < (sizeof(g_bench_schemes) / sizeof(g_bench_schemes[0])); ++i) {
        if (g_bench_schemes[i].scheme == scheme) {
            return &g_bench_schemes[i];
        }
    }
    return NULL;
}

static const char *table_scheme_name(patch_scheme_t scheme) {
    const benchmark_scheme_ops_t *entry = find_bench_scheme(scheme);

    if (entry == NULL) {
        return patch_scheme_name(scheme);
    }
    return entry->table_name;
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

static uint32_t cycles_add(uint32_t a, uint32_t b) {
    uint64_t total = 0u;

    if (a == 0xFFFFFFFFu || b == 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    total = (uint64_t)a + (uint64_t)b;
    if (total > 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)total;
}

static uint32_t cycles_add3(uint32_t a, uint32_t b, uint32_t c) {
    return cycles_add(cycles_add(a, b), c);
}

static void prepare_runtime_regs(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3) {
    cve_target_input_t input = {0};

    if (r0 == NULL || r1 == NULL || r2 == NULL || r3 == NULL) {
        return;
    }

    cve_target_get_attack_inputs(&input);
    *r0 = 0u;
    *r1 = 0u;
    *r2 = 0u;
    *r3 = 0u;

    if (input.target == CVE_TARGET_CVE2024_2212) {
        *r0 = (uint32_t)input.queue_length;
        *r1 = (uint32_t)input.item_size;
        return;
    }

    if (input.target == CVE_TARGET_CVE2025_1674) {
        *r0 = 12u;
        *r1 = 18u;
        *r2 = 3u;
        *r3 = 3u;
        return;
    }

    *r0 = 4u;
    *r1 = 6u;
    *r2 = 128u;
    *r3 = 0u;
}

static void prepare_benign_runtime_regs(uint32_t *r0, uint32_t *r1, uint32_t *r2, uint32_t *r3) {
    cve_target_input_t input = {0};

    if (r0 == NULL || r1 == NULL || r2 == NULL || r3 == NULL) {
        return;
    }

    cve_target_get_attack_inputs(&input);
    *r0 = 0u;
    *r1 = 0u;
    *r2 = 0u;
    *r3 = 0u;

    if (input.target == CVE_TARGET_CVE2024_2212) {
        /*
         * benign for overflow guard:
         * queue_length=2, item_size=4 -> valid and non-overflowing
         */
        *r0 = 2u;
        *r1 = 4u;
        return;
    }

    if (input.target == CVE_TARGET_CVE2025_1674) {
        /*
         * benign for DNS bounds guard:
         * answer_offset=12, msg_size=32, dname_len=3, rem_size=17 -> PASS
         */
        *r0 = 12u;
        *r1 = 32u;
        *r2 = 3u;
        *r3 = 17u;
        return;
    }

    /*
     * benign for ICMP family guard:
     * pkt_family=IPv4(4), handler_family=IPv4(4), type=128, code=0 -> PASS
     */
    *r0 = 4u;
    *r1 = 4u;
    *r2 = 128u;
    *r3 = 0u;
}

static bool measure_invoke_once(const scheme_ops_t *ops,
                                uint32_t r0,
                                uint32_t r1,
                                uint32_t r2,
                                uint32_t r3,
                                uint32_t *cycles,
                                int *ret_code) {
    int call_ret = -999;
    uint32_t call_cycles = 0xFFFFFFFFu;

    if (cycles != NULL) {
        *cycles = 0xFFFFFFFFu;
    }
    if (ret_code != NULL) {
        *ret_code = -999;
    }
    if (ops == NULL || ops->invoke == NULL) {
        return false;
    }

    if (!cycle_counter_reset()) {
        return false;
    }
    call_ret = ops->invoke(r0, r1, r2, r3);
    call_cycles = cycle_counter_read();

    if (ret_code != NULL) {
        *ret_code = call_ret;
    }
    if (cycles != NULL) {
        *cycles = call_cycles;
    }
    return call_cycles != 0xFFFFFFFFu;
}

static bool measure_invoke_loop(const scheme_ops_t *ops,
                                uint32_t iterations,
                                bool expect_fixed,
                                uint32_t r0,
                                uint32_t r1,
                                uint32_t r2,
                                uint32_t r3,
                                uint32_t *cycles,
                                int *last_ret_code) {
    int ret_code = -999;
    uint32_t completed = 0u;
    uint32_t call_cycles = 0xFFFFFFFFu;

    if (cycles != NULL) {
        *cycles = 0xFFFFFFFFu;
    }
    if (last_ret_code != NULL) {
        *last_ret_code = -999;
    }
    if (ops == NULL || ops->invoke == NULL || iterations == 0u) {
        return false;
    }

    if (!cycle_counter_reset()) {
        return false;
    }

    for (uint32_t i = 0; i < iterations; ++i) {
        ret_code = ops->invoke(r0, r1, r2, r3);
        completed++;
        if (expect_fixed && !patch_result_is_fixed(ret_code)) {
            return false;
        }
    }

    call_cycles = cycle_counter_read();
    if (last_ret_code != NULL) {
        *last_ret_code = ret_code;
    }
    if (cycles != NULL) {
        *cycles = call_cycles;
    }
    return completed == iterations && call_cycles != 0xFFFFFFFFu;
}

static bool measure_install(const scheme_ops_t *ops, bool *install_ok, uint32_t *install_cycles) {
    bool ok = false;
    uint32_t cycles = 0xFFFFFFFFu;

    if (install_ok != NULL) {
        *install_ok = false;
    }
    if (install_cycles != NULL) {
        *install_cycles = 0xFFFFFFFFu;
    }
    if (ops == NULL || ops->install == NULL) {
        return false;
    }

    if (!cycle_counter_reset()) {
        return false;
    }
    ok = ops->install();
    cycles = cycle_counter_read();

    if (install_ok != NULL) {
        *install_ok = ok;
    }
    if (install_cycles != NULL) {
        *install_cycles = cycles;
    }
    return cycles != 0xFFFFFFFFu;
}

static bool measure_uninstall(const scheme_ops_t *ops, uint32_t *cycles) {
    uint32_t value = 0xFFFFFFFFu;

    if (cycles != NULL) {
        *cycles = 0xFFFFFFFFu;
    }
    if (ops == NULL || ops->uninstall == NULL) {
        return false;
    }

    if (!cycle_counter_reset()) {
        return false;
    }
    ops->uninstall();
    value = cycle_counter_read();

    if (cycles != NULL) {
        *cycles = value;
    }
    return value != 0xFFFFFFFFu;
}

static void prepare_scheme_baseline(const benchmark_scheme_ops_t *entry) {
    if (entry == NULL) {
        return;
    }

    if (entry->scheme != PATCH_SCHEME_LEGACY && entry->ops.uninstall != NULL) {
        entry->ops.uninstall();
    }
}

patch_txn_benchmark_result_t benchmark_run_for_scheme(patch_scheme_t scheme) {
    patch_txn_benchmark_result_t result = {
        .available = false,
        .install_ok = false,
        .baseline_ok = false,
        .pass_ok = false,
        .first_fix_ok = false,
        .steady_ok = false,
        .uninstall_ok = false,
        .trigger_is_approx = false,
        .pass_is_approx = false,
        .baseline_ret_code = -999,
        .benign_baseline_ret_code = -999,
        .pass_ret_code = -999,
        .first_fix_ret_code = -999,
        .steady_last_ret_code = -999,
        .uninstalled_ret_code = -999,
        .t_background = 0xFFFFFFFFu,
        .t_pass_only = 0xFFFFFFFFu,
        .t_install = 0xFFFFFFFFu,
        .t_trigger_only = 0xFFFFFFFFu,
        .t_dispatch_only_1 = 0xFFFFFFFFu,
        .t_dispatch_only_5 = 0xFFFFFFFFu,
        .t_dispatch_only_64 = 0xFFFFFFFFu,
        .t_patch_exec_only = 0xFFFFFFFFu,
        .t_first_fix = 0xFFFFFFFFu,
        .t_steady_100 = 0xFFFFFFFFu,
        .t_uninstall = 0xFFFFFFFFu,
        .t_roundtrip = 0xFFFFFFFFu,
    };
    const benchmark_scheme_ops_t *entry = find_bench_scheme(scheme);
    uint32_t r0 = 0u;
    uint32_t r1 = 0u;
    uint32_t r2 = 0u;
    uint32_t r3 = 0u;
    uint32_t br0 = 0u;
    uint32_t br1 = 0u;
    uint32_t br2 = 0u;
    uint32_t br3 = 0u;
    uint32_t steady_cycles_total = 0xFFFFFFFFu;
    int post_uninstall_ret = -999;
    bool cleanup_needed = false;

    if (entry == NULL) {
        return result;
    }

    if (entry->memory_cost != NULL) {
        result.mem_cost = entry->memory_cost();
    }

    if (!patch_demo_can_run(scheme)) {
        SEGGER_RTT_printf(0,
            "[-] %s benchmark requires a pristine flash image. Reflash/reset before rerunning it.\r\n",
            patch_scheme_name(scheme));
        return result;
    }

    prepare_runtime_regs(&r0, &r1, &r2, &r3);
    prepare_benign_runtime_regs(&br0, &br1, &br2, &br3);
    result.pass_is_approx = entry->pass_is_approx_when_arg_ignored;
    prepare_scheme_baseline(entry);
    app_set_exec_mode(APP_EXEC_MODE_BENCHMARK);

    if (!measure_invoke_once(&entry->ops, r0, r1, r2, r3, &result.t_background, &result.baseline_ret_code)) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }
    result.baseline_ok = patch_result_is_vulnerable(result.baseline_ret_code);
    if (!result.baseline_ok) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        SEGGER_RTT_printf(0,
            "[-] %s baseline is not vulnerable under benchmark input, ret=%d.\r\n",
            patch_scheme_name(scheme),
            result.baseline_ret_code);
        return result;
    }

    if (!measure_invoke_once(&entry->ops, br0, br1, br2, br3, NULL, &result.benign_baseline_ret_code)) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }

    if (!measure_install(&entry->ops, &result.install_ok, &result.t_install) || !result.install_ok) {
        if (entry->ops.uninstall != NULL) {
            entry->ops.uninstall();
        }
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }
    cleanup_needed = true;

    if (!measure_invoke_once(&entry->ops, br0, br1, br2, br3, &result.t_pass_only, &result.pass_ret_code)) {
        if (cleanup_needed && entry->ops.uninstall != NULL) {
            entry->ops.uninstall();
        }
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }
    result.pass_ok = (result.pass_ret_code == result.benign_baseline_ret_code);

    if (!measure_invoke_once(&entry->ops, r0, r1, r2, r3, &result.t_first_fix, &result.first_fix_ret_code)) {
        if (cleanup_needed && entry->ops.uninstall != NULL) {
            entry->ops.uninstall();
        }
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }
    result.first_fix_ok = patch_result_is_fixed(result.first_fix_ret_code);

    result.steady_ok = measure_invoke_loop(
        &entry->ops,
        BENCHMARK_STEADY_CALLS,
        true,
        r0,
        r1,
        r2,
        r3,
        &steady_cycles_total,
        &result.steady_last_ret_code);
    if (result.steady_ok) {
        result.t_steady_100 = steady_cycles_total / BENCHMARK_STEADY_CALLS;
    }

    if (entry->bench_trigger_only != NULL) {
        result.t_trigger_only = entry->bench_trigger_only();
    } else {
        result.trigger_is_approx = entry->trigger_is_approx_when_missing;
        (void)measure_invoke_once(&entry->ops, r0, r1, r2, r3, &result.t_trigger_only, NULL);
    }

    if (entry->bench_dispatch_only != NULL) {
        result.t_dispatch_only_1 = entry->bench_dispatch_only(1u);
        result.t_dispatch_only_5 = entry->bench_dispatch_only(5u);
        result.t_dispatch_only_64 = entry->bench_dispatch_only(64u);
    }

    if (entry->bench_patch_exec_only != NULL) {
        result.t_patch_exec_only = entry->bench_patch_exec_only();
    }

    if (!measure_uninstall(&entry->ops, &result.t_uninstall)) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }
    cleanup_needed = false;
    result.t_roundtrip = cycles_add3(result.t_install, result.t_first_fix, result.t_uninstall);

    if (!measure_invoke_once(&entry->ops, r0, r1, r2, r3, NULL, &post_uninstall_ret)) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }
    result.uninstalled_ret_code = post_uninstall_ret;
    result.uninstall_ok = patch_result_is_vulnerable(result.uninstalled_ret_code);
    result.available = true;

    app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
    return result;
}

static void print_runtime_table(const patch_scheme_t *schemes,
                                const patch_txn_benchmark_result_t *results,
                                size_t count) {
    console_puts("\r\n=== Runtime Hotpatch Overhead (cycles) ===\r\n");
    SEGGER_RTT_printf(0,
        "scheme             T_background T_pass_only T_install   T_trigger_only T_dispatch_1 T_dispatch_5 T_dispatch_64 T_patch_exec T_first_fix T_steady_100 T_uninstall T_roundtrip\r\n");

    for (size_t i = 0; i < count; ++i) {
        char bg[16];
        char pass_only[16];
        char install[16];
        char trigger[16];
        char d1[16];
        char d5[16];
        char d64[16];
        char exec_only[16];
        char first_fix[16];
        char steady[16];
        char uninstall[16];
        char roundtrip[16];

        format_cycles(bg, sizeof(bg), results[i].t_background);
        format_cycles(pass_only, sizeof(pass_only), results[i].t_pass_only);
        format_cycles(install, sizeof(install), results[i].t_install);
        format_cycles(trigger, sizeof(trigger), results[i].t_trigger_only);
        format_cycles(d1, sizeof(d1), results[i].t_dispatch_only_1);
        format_cycles(d5, sizeof(d5), results[i].t_dispatch_only_5);
        format_cycles(d64, sizeof(d64), results[i].t_dispatch_only_64);
        format_cycles(exec_only, sizeof(exec_only), results[i].t_patch_exec_only);
        format_cycles(first_fix, sizeof(first_fix), results[i].t_first_fix);
        format_cycles(steady, sizeof(steady), results[i].t_steady_100);
        format_cycles(uninstall, sizeof(uninstall), results[i].t_uninstall);
        format_cycles(roundtrip, sizeof(roundtrip), results[i].t_roundtrip);

        SEGGER_RTT_printf(0,
            "%-18s %-12s %-12s %-11s %-14s %-12s %-12s %-13s %-12s %-11s %-12s %-11s %-11s\r\n",
            table_scheme_name(schemes[i]),
            bg,
            pass_only,
            install,
            trigger,
            d1,
            d5,
            d64,
            exec_only,
            first_fix,
            steady,
            uninstall,
            roundtrip);
    }
}

static void print_return_table(const patch_scheme_t *schemes,
                               const patch_txn_benchmark_result_t *results,
                               size_t count) {
    console_puts("\r\n=== Return-Code Validation ===\r\n");
    SEGGER_RTT_printf(0,
        "scheme             baseline          pass_ret          first_fix         steady_last       after_uninstall   baseline_ok pass_ok pass_approx first_fix_ok steady_ok uninstall_ok\r\n");

    for (size_t i = 0; i < count; ++i) {
        char baseline[24];
        char pass_ret[24];
        char first_fix[24];
        char steady_last[24];
        char uninstalled[24];

        format_result(baseline, sizeof(baseline), results[i].baseline_ret_code);
        format_result(pass_ret, sizeof(pass_ret), results[i].pass_ret_code);
        format_result(first_fix, sizeof(first_fix), results[i].first_fix_ret_code);
        format_result(steady_last, sizeof(steady_last), results[i].steady_last_ret_code);
        format_result(uninstalled, sizeof(uninstalled), results[i].uninstalled_ret_code);

        SEGGER_RTT_printf(0,
            "%-18s %-17s %-17s %-17s %-17s %-17s %-11s %-7s %-11s %-12s %-9s %-12s\r\n",
            table_scheme_name(schemes[i]),
            baseline,
            pass_ret,
            first_fix,
            steady_last,
            uninstalled,
            results[i].baseline_ok ? "yes" : "no",
            results[i].pass_ok ? "yes" : "no",
            results[i].pass_is_approx ? "yes" : "no",
            results[i].first_fix_ok ? "yes" : "no",
            results[i].steady_ok ? "yes" : "no",
            results[i].uninstall_ok ? "yes" : "no");
    }
}

static void print_runtime_notes(const patch_scheme_t *schemes,
                                const patch_txn_benchmark_result_t *results,
                                size_t count) {
    bool has_trigger_approx = false;
    bool has_pass_approx = false;

    for (size_t i = 0; i < count; ++i) {
        if (results[i].trigger_is_approx) {
            has_trigger_approx = true;
        }
        if (results[i].pass_is_approx) {
            has_pass_approx = true;
        }
    }

    console_puts("\r\n[note] This compare only measures runtime hotpatch overhead after patch payload is ready.\r\n");
    console_puts("[note] RapidPatch excludes eBPF rewrite/verifier/JIT/offline generation; runtime path is fixed patch point + dispatcher + VM.\r\n");
    console_puts("[note] AutoPatch excludes offline auto-generation/LLVM analysis; runtime path is static trampoline + dispatcher + hotpatch body.\r\n");
    if (has_trigger_approx) {
        console_puts("[note] For ClearBitPatch/HERA, T_trigger_only is a closest-possible approximation because trigger and payload are tightly coupled in hardware redirection path.\r\n");
    }
    if (has_pass_approx) {
        console_puts("[note] For ClearBitPatch/HERA, T_pass_only is a closest-possible measurement because their invoke path does not currently consume unified r0..r3 runtime args.\r\n");
    }
    console_puts("[note] T_pass_only is measured with patch installed and benign input; path still goes through full runtime invoke path and should continue instead of DROP/REDIRECT.\r\n");
    (void)schemes;
}

static void print_memory_table(const patch_scheme_t *schemes,
                               const patch_txn_benchmark_result_t *results,
                               size_t count) {
    console_puts("\r\n=== Memory Cost (bytes) ===\r\n");
    SEGGER_RTT_printf(0,
        "scheme             flash       ram\r\n");

    for (size_t i = 0; i < count; ++i) {
        SEGGER_RTT_printf(0,
            "%-18s %-11lu %-11lu\r\n",
            table_scheme_name(schemes[i]),
            (unsigned long)results[i].mem_cost.flash_bytes,
            (unsigned long)results[i].mem_cost.ram_bytes);
    }
}

void benchmark_print_single_scheme(patch_scheme_t scheme, const patch_txn_benchmark_result_t *result) {
    print_runtime_table(&scheme, result, 1u);
    print_return_table(&scheme, result, 1u);
    print_memory_table(&scheme, result, 1u);
    print_runtime_notes(&scheme, result, 1u);
}

void benchmark_run_and_print(patch_scheme_t scheme) {
    patch_txn_benchmark_result_t result = benchmark_run_for_scheme(scheme);

    SEGGER_RTT_printf(0,
        "\r\n=== Benchmark [%s / %s] ===\r\n",
        patch_scheme_name(scheme),
        cve_target_name(cve_target_get_current()));
    benchmark_print_single_scheme(scheme, &result);
}

void benchmark_compare_and_print(void) {
    patch_txn_benchmark_result_t results[sizeof(g_bench_schemes) / sizeof(g_bench_schemes[0])];
    patch_scheme_t schemes[sizeof(g_bench_schemes) / sizeof(g_bench_schemes[0])];

    SEGGER_RTT_printf(0,
        "\r\n=== Runtime Hotpatch Compare [%s] ===\r\n",
        cve_target_name(cve_target_get_current()));

    for (size_t i = 0; i < (sizeof(g_bench_schemes) / sizeof(g_bench_schemes[0])); ++i) {
        schemes[i] = g_bench_schemes[i].scheme;
        results[i] = benchmark_run_for_scheme(g_bench_schemes[i].scheme);
    }

    print_runtime_table(schemes, results, sizeof(results) / sizeof(results[0]));
    print_return_table(schemes, results, sizeof(results) / sizeof(results[0]));
    print_memory_table(schemes, results, sizeof(results) / sizeof(results[0]));
    print_runtime_notes(schemes, results, sizeof(results) / sizeof(results[0]));
}
