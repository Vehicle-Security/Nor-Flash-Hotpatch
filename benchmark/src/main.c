#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "SEGGER_RTT.h"
#include "bsp.h"
#include "nrf.h"

#include "app_common.h"
#include "autopatch_mode.h"
#include "cycle_counter.h"
#include "patch_control.h"
#include "patch_result.h"

typedef struct {
    bool available;
    bool apply_ok;
    bool baseline_ok;
    bool first_fix_ok;
    bool fix_ok;
    bool unfix_ok;
    uint32_t patched_call_count;
    uint32_t t_fix_first_cycles;
    uint32_t t_fix_cycles;
    uint32_t t_steady_cycles;
    uint32_t t_unfix_cycles;
    uint32_t t_roundtrip_cycles;
    int baseline_ret_code;
    int first_fix_ret_code;
    int fix_ret_code;
    int unfix_ret_code;
} patch_txn_benchmark_result_t;

#ifndef APP_STARTUP_SMOKE_TEST
#define APP_STARTUP_SMOKE_TEST 0
#endif

#define BENCHMARK_PATCHED_CALLS 100u

static app_exec_mode_t g_exec_mode = APP_EXEC_MODE_INTERACTIVE;
static patch_scheme_t g_current_scheme = PATCH_SCHEME_RAPID;

static const UBaseType_t g_demo_uxQueueLength = 0x40000001u;
static const UBaseType_t g_demo_uxItemSize    = 0x00000004u;

static const patch_scheme_t g_compare_order[] = {
    PATCH_SCHEME_RAPID,
    PATCH_SCHEME_HERA,
    PATCH_SCHEME_AUTOPATCH,
    PATCH_SCHEME_LEGACY,
};

void console_init(void) {
    SEGGER_RTT_Init();
    SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);
    SEGGER_RTT_WriteString(0, "\r\nRTT console ready. (Fair Hotpatch Benchmark)\r\n");
}

void console_puts(const char *s) {
    SEGGER_RTT_WriteString(0, s);
}

void console_prompt(void) {
    console_puts("rtt> ");
}

void app_set_exec_mode(app_exec_mode_t mode) {
    g_exec_mode = mode;
}

app_exec_mode_t app_get_exec_mode(void) {
    return g_exec_mode;
}

bool app_exec_mode_is_verbose(void) {
    return g_exec_mode != APP_EXEC_MODE_BENCHMARK;
}

void app_get_attack_inputs(UBaseType_t *queue_length, UBaseType_t *item_size) {
    if (queue_length != NULL) {
        *queue_length = g_demo_uxQueueLength;
    }
    if (item_size != NULL) {
        *item_size = g_demo_uxItemSize;
    }
}

bool app_fetch_auto_inputs(UBaseType_t *queue_length, UBaseType_t *item_size) {
    if (g_exec_mode == APP_EXEC_MODE_INTERACTIVE) {
        return false;
    }

    app_get_attack_inputs(queue_length, item_size);
    return true;
}

static const char *yes_no(bool value) {
    return value ? "yes" : "no";
}

static bool parse_scheme_name(const char *text, patch_scheme_t *scheme) {
    if (strcmp(text, "legacy") == 0) {
        *scheme = PATCH_SCHEME_LEGACY;
        return true;
    }
    if (strcmp(text, "rapid") == 0) {
        *scheme = PATCH_SCHEME_RAPID;
        return true;
    }
    if (strcmp(text, "hera") == 0) {
        *scheme = PATCH_SCHEME_HERA;
        return true;
    }
    if (strcmp(text, "autopatch") == 0) {
        *scheme = PATCH_SCHEME_AUTOPATCH;
        return true;
    }
    return false;
}

static void print_mode_line(void) {
    SEGGER_RTT_printf(0, "[mode] current scheme: %s\r\n", patch_scheme_name(g_current_scheme));
}

static void prepare_scheme_baseline(patch_scheme_t scheme) {
    if (scheme != PATCH_SCHEME_LEGACY) {
        patch_unapply(scheme);
    }
}

static bool scheme_requires_offline_compile(patch_scheme_t scheme) {
    return scheme == PATCH_SCHEME_AUTOPATCH;
}

static bool scheme_requires_pristine_flash(patch_scheme_t scheme) {
    return scheme == PATCH_SCHEME_LEGACY;
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

static void format_avg_cycles(char *buf, size_t buf_size, const patch_txn_benchmark_result_t *result) {
    if (result->patched_call_count <= 1u || result->t_steady_cycles == 0xFFFFFFFFu) {
        (void)snprintf(buf, buf_size, "N/A");
        return;
    }

    (void)snprintf(
        buf,
        buf_size,
        "%lu",
        (unsigned long)(result->t_steady_cycles / (result->patched_call_count - 1u)));
}

static void print_exec_result(const char *stage, int ret_code) {
    SEGGER_RTT_printf(0,
        "[result] %-8s ret=%d (%s) fixed=%s\r\n",
        stage,
        ret_code,
        patch_result_name(ret_code),
        yes_no(patch_result_is_fixed(ret_code)));
}

static uint32_t cycles_delta(uint32_t end_cycles, uint32_t start_cycles) {
    if (end_cycles == 0xFFFFFFFFu || start_cycles == 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    return end_cycles - start_cycles;
}

static void run_demo_for_scheme(patch_scheme_t scheme) {
    UBaseType_t queue_length = 0;
    UBaseType_t item_size = 0;
    int ret_code = 0;

    if (!patch_demo_can_run(scheme)) {
        SEGGER_RTT_printf(0,
            "[-] %s demo requires a pristine flash image. Reflash/reset before rerunning it.\r\n",
            patch_scheme_name(scheme));
        return;
    }

    prepare_scheme_baseline(scheme);
    app_set_exec_mode(APP_EXEC_MODE_DEMO);
    app_get_attack_inputs(&queue_length, &item_size);

    SEGGER_RTT_printf(0,
        "\r\n--- Demo Start [%s] ---\r\n"
        "[demo] unified attack input: uxQueueLength=0x%08X, uxItemSize=0x%08X\r\n",
        patch_scheme_name(scheme),
        (uint32_t)queue_length,
        (uint32_t)item_size);

    console_puts("1. Initial call (baseline vulnerable path):\r\n");
    ret_code = patch_call(scheme);
    print_exec_result("baseline", ret_code);

    console_puts("\r\n2. Applying patch...\r\n");
    if (!patch_apply(scheme)) {
        SEGGER_RTT_printf(0, "[-] %s patch apply failed.\r\n", patch_scheme_name(scheme));
    }
    print_patch_status(scheme);

    console_puts("3. Call after patch (same attack input):\r\n");
    ret_code = patch_call(scheme);
    print_exec_result("patched", ret_code);

    console_puts("\r\n4. Removing patch...\r\n");
    patch_unapply(scheme);
    print_patch_status(scheme);

    console_puts("5. Call after unpatch (same attack input):\r\n");
    ret_code = patch_call(scheme);
    print_exec_result("unfixed", ret_code);

    app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
    SEGGER_RTT_printf(0, "--- Demo End [%s] ---\r\n", patch_scheme_name(scheme));
}

static void run_demo(void) {
    run_demo_for_scheme(g_current_scheme);
}

static patch_txn_benchmark_result_t run_txn_benchmark_for_scheme(patch_scheme_t scheme) {
    patch_txn_benchmark_result_t result = {
        .available = false,
        .apply_ok = false,
        .baseline_ok = false,
        .first_fix_ok = false,
        .fix_ok = false,
        .unfix_ok = false,
        .patched_call_count = 0u,
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
    uint32_t unfix_start = 0xFFFFFFFFu;
    uint32_t unfix_end = 0xFFFFFFFFu;

    if (!patch_demo_can_run(scheme)) {
        SEGGER_RTT_printf(0,
            "[-] %s benchmark requires a pristine flash image. Reflash/reset before rerunning it.\r\n",
            patch_scheme_name(scheme));
        return result;
    }

    prepare_scheme_baseline(scheme);
    app_set_exec_mode(APP_EXEC_MODE_BENCHMARK);

    result.baseline_ret_code = patch_call(scheme);
    result.baseline_ok = patch_result_is_vulnerable(result.baseline_ret_code);

    if (!result.baseline_ok) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        SEGGER_RTT_printf(0,
            "[-] %s baseline is not vulnerable under the unified attack input, ret=%d.\r\n",
            patch_scheme_name(scheme),
            result.baseline_ret_code);
        return result;
    }

    if (!cycle_counter_reset()) {
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return result;
    }

    result.apply_ok = patch_apply(scheme);
    for (uint32_t i = 0; i < BENCHMARK_PATCHED_CALLS; ++i) {
        int ret_code = patch_call(scheme);

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

    unfix_start = fix_end;
    patch_unapply(scheme);
    result.t_roundtrip_cycles = cycle_counter_read();

    result.unfix_ret_code = patch_call(scheme);
    unfix_end = cycle_counter_read();

    result.t_fix_first_cycles = first_fix_end;
    result.t_fix_cycles = fix_end;
    result.t_steady_cycles = cycles_delta(fix_end, first_fix_end);
    result.t_unfix_cycles = cycles_delta(unfix_end, unfix_start);
    result.first_fix_ok = result.apply_ok && patch_result_is_fixed(result.first_fix_ret_code);
    result.fix_ok = result.apply_ok
        && all_fixed
        && (result.patched_call_count == BENCHMARK_PATCHED_CALLS);
    result.unfix_ok = patch_result_is_vulnerable(result.unfix_ret_code);
    result.available = true;

    app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
    return result;
}

static void print_first_hit_table(const patch_scheme_t *schemes,
                                  const patch_txn_benchmark_result_t *results,
                                  size_t count) {
    console_puts("\r\n=== Table 1A: First-Hit Recovery ===\r\n");
    console_puts("scheme     baseline         first_fix_ret    first_ok  T_fix(1x)\r\n");

    for (size_t i = 0; i < count; ++i) {
        char baseline_buf[24];
        char first_fix_buf[24];
        char fix_cycles[16];

        format_result(baseline_buf, sizeof(baseline_buf), results[i].baseline_ret_code);
        format_result(first_fix_buf, sizeof(first_fix_buf), results[i].first_fix_ret_code);
        format_cycles(fix_cycles, sizeof(fix_cycles), results[i].t_fix_first_cycles);

        SEGGER_RTT_printf(0,
            "%-10s %-16s %-16s %-8s %-10s\r\n",
            patch_scheme_name(schemes[i]),
            baseline_buf,
            first_fix_buf,
            yes_no(results[i].first_fix_ok),
            fix_cycles);
    }
}

static void print_steady_state_table(const patch_scheme_t *schemes,
                                     const patch_txn_benchmark_result_t *results,
                                     size_t count) {
    console_puts("\r\n=== Table 1B: Steady-State Patched Cost ===\r\n");
    SEGGER_RTT_printf(0,
        "scheme     last_fix_ret     fix_ok  patch_calls  T_fix(100x)  T_steady     avg_steady  unfix_ret        unfix_ok  T_unfix  T_roundtrip\r\n");

    for (size_t i = 0; i < count; ++i) {
        char fix_buf[24];
        char unfix_buf[24];
        char fix_cycles[16];
        char steady_cycles[16];
        char avg_cycles[16];
        char unfix_cycles[16];
        char roundtrip_cycles[16];

        format_result(fix_buf, sizeof(fix_buf), results[i].fix_ret_code);
        format_result(unfix_buf, sizeof(unfix_buf), results[i].unfix_ret_code);
        format_cycles(fix_cycles, sizeof(fix_cycles), results[i].t_fix_cycles);
        format_cycles(steady_cycles, sizeof(steady_cycles), results[i].t_steady_cycles);
        format_avg_cycles(avg_cycles, sizeof(avg_cycles), &results[i]);
        format_cycles(unfix_cycles, sizeof(unfix_cycles), results[i].t_unfix_cycles);
        format_cycles(roundtrip_cycles, sizeof(roundtrip_cycles), results[i].t_roundtrip_cycles);

        SEGGER_RTT_printf(0,
            "%-10s %-16s %-7s %-12lu %-12s %-12s %-11s %-16s %-9s %-9s %-11s\r\n",
            patch_scheme_name(schemes[i]),
            fix_buf,
            yes_no(results[i].fix_ok),
            (unsigned long)results[i].patched_call_count,
            fix_cycles,
            steady_cycles,
            avg_cycles,
            unfix_buf,
            yes_no(results[i].unfix_ok),
            unfix_cycles,
            roundtrip_cycles);
    }
}

static void print_deployment_table(const patch_scheme_t *schemes, size_t count) {
    console_puts("\r\n=== Table 2: Deployment Cost ===\r\n");
    console_puts("scheme     offline_compile  online_hot_toggle  pristine_flash\r\n");

    for (size_t i = 0; i < count; ++i) {
        SEGGER_RTT_printf(0,
            "%-10s %-16s %-18s %-14s\r\n",
            patch_scheme_name(schemes[i]),
            yes_no(scheme_requires_offline_compile(schemes[i])),
            yes_no(patch_supports_online_toggle(schemes[i])),
            yes_no(scheme_requires_pristine_flash(schemes[i])));
    }

    console_puts("[note] AutoPatch online metrics reflect deployment-ready activation latency via the software enable switch.\r\n");
    SEGGER_RTT_printf(0,
        "[note] T_fix(1x) captures first recovery latency. T_fix(%lux) includes patch apply plus %lu patched calls.\r\n",
        (unsigned long)BENCHMARK_PATCHED_CALLS,
        (unsigned long)BENCHMARK_PATCHED_CALLS);
    SEGGER_RTT_printf(0,
        "[note] T_steady isolates calls 2..%lu. T_roundtrip includes apply -> %lu patched calls -> unapply.\r\n",
        (unsigned long)BENCHMARK_PATCHED_CALLS,
        (unsigned long)BENCHMARK_PATCHED_CALLS);
}

static void print_single_scheme_benchmark(patch_scheme_t scheme, const patch_txn_benchmark_result_t *result) {
    print_first_hit_table(&scheme, result, 1u);
    print_steady_state_table(&scheme, result, 1u);
    print_deployment_table(&scheme, 1u);
}

static void run_benchmark(void) {
    patch_txn_benchmark_result_t result = run_txn_benchmark_for_scheme(g_current_scheme);
    print_single_scheme_benchmark(g_current_scheme, &result);
}

static void run_compare(void) {
    patch_txn_benchmark_result_t results[sizeof(g_compare_order) / sizeof(g_compare_order[0])];

    console_puts("\r\n=== Fair Benchmark Compare ===\r\n");

    for (size_t i = 0; i < (sizeof(g_compare_order) / sizeof(g_compare_order[0])); ++i) {
        results[i] = run_txn_benchmark_for_scheme(g_compare_order[i]);
    }

    print_first_hit_table(g_compare_order, results, sizeof(results) / sizeof(results[0]));
    print_steady_state_table(g_compare_order, results, sizeof(results) / sizeof(results[0]));
    print_deployment_table(g_compare_order, sizeof(results) / sizeof(results[0]));
}

static void print_help(void) {
    console_puts("commands: help, mode legacy|rapid|hera|autopatch, demo, bench, compare, call, patch, unpatch, status\r\n");
}

static void print_status(void) {
    print_mode_line();
    print_all_patch_status();
}

static void run_startup_smoke_test(void) {
#if APP_STARTUP_SMOKE_TEST
    console_puts("\r\n=== Startup Smoke Test ===\r\n");
    run_demo_for_scheme(PATCH_SCHEME_AUTOPATCH);
    run_compare();
#endif
}

static void handle_command(const char *cmd) {
    patch_scheme_t scheme = PATCH_SCHEME_RAPID;

    if (cmd[0] == '\0') {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        print_help();
        return;
    }

    if (strcmp(cmd, "demo") == 0) {
        run_demo();
        return;
    }

    if (strcmp(cmd, "bench") == 0) {
        run_benchmark();
        return;
    }

    if (strcmp(cmd, "compare") == 0) {
        run_compare();
        return;
    }

    if (strcmp(cmd, "call") == 0) {
        print_exec_result("call", patch_call(g_current_scheme));
        return;
    }

    if (strcmp(cmd, "patch") == 0) {
        if (!patch_apply(g_current_scheme)) {
            SEGGER_RTT_printf(0, "[-] %s patch apply failed.\r\n", patch_scheme_name(g_current_scheme));
        }
        print_patch_status(g_current_scheme);
        return;
    }

    if (strcmp(cmd, "unpatch") == 0) {
        patch_unapply(g_current_scheme);
        print_patch_status(g_current_scheme);
        return;
    }

    if (strcmp(cmd, "status") == 0) {
        print_status();
        return;
    }

    if (strncmp(cmd, "mode ", 5) == 0) {
        if (!parse_scheme_name(cmd + 5, &scheme)) {
            SEGGER_RTT_printf(0, "[-] unknown mode: %s\r\n", cmd + 5);
            return;
        }
        g_current_scheme = scheme;
        print_mode_line();
        return;
    }

    if (strncmp(cmd, "demo ", 5) == 0) {
        if (!parse_scheme_name(cmd + 5, &scheme)) {
            SEGGER_RTT_printf(0, "[-] unknown demo target: %s\r\n", cmd + 5);
            return;
        }
        run_demo_for_scheme(scheme);
        return;
    }

    if (strncmp(cmd, "bench ", 6) == 0) {
        if (!parse_scheme_name(cmd + 6, &scheme)) {
            SEGGER_RTT_printf(0, "[-] unknown benchmark target: %s\r\n", cmd + 6);
            return;
        }
        {
            patch_txn_benchmark_result_t result = run_txn_benchmark_for_scheme(scheme);
            print_single_scheme_benchmark(scheme, &result);
        }
        return;
    }

    SEGGER_RTT_printf(0, "unknown command: %s\r\n", cmd);
}

int main(void) {
    char cmd[48];
    size_t len = 0;

    bsp_board_init(BSP_INIT_LEDS);
    console_init();

    if (cycle_counter_init()) {
        console_puts("[init] DWT cycle counter ready.\r\n");
    } else {
        console_puts("[init] Warning: DWT cycle counter unavailable.\r\n");
    }

    print_help();
    print_status();
    run_startup_smoke_test();
    console_prompt();

    while (true) {
        int key = SEGGER_RTT_GetKey();
        if (key < 0) {
            continue;
        }

        if (key == '\r' || key == '\n') {
            console_puts("\r\n");
            cmd[len] = '\0';
            handle_command(cmd);
            len = 0;
            console_prompt();
            continue;
        }

        if ((key == '\b' || key == 0x7F) && len > 0u) {
            len--;
            console_puts("\b \b");
            continue;
        }

        if (key >= 0x20 && key <= 0x7E && len < (sizeof(cmd) - 1u)) {
            cmd[len++] = (char)key;
            SEGGER_RTT_PutChar(0, (char)key);
        }
    }
}
