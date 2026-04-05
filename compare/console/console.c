#include "console.h"

#include <stdarg.h>
#include <string.h>

#include "SEGGER_RTT.h"

int SEGGER_RTT_vprintf(unsigned BufferIndex, const char *sFormat, va_list *pParamList);

#include "core/app/app_mode.h"
#include "compare/benchmark/benchmark.h"
#include "compare/patch/patch_control.h"
#include "core/patch/patch_result.h"
#include "core/target/cve_target.h"

#ifndef APP_STARTUP_SMOKE_TEST
#define APP_STARTUP_SMOKE_TEST 0
#endif

static patch_scheme_t g_current_scheme = PATCH_SCHEME_RAPID;

void console_init(void) {
    SEGGER_RTT_Init();
    SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_NO_BLOCK_TRIM);
    SEGGER_RTT_WriteString(0, "\r\nRTT console ready. (Fair Hotpatch Benchmark)\r\n");
}

void console_puts(const char *s) {
    SEGGER_RTT_WriteString(0, s);
}

int console_printf(const char *format, ...) {
    va_list args;
    int ret;
    va_start(args, format);
    ret = SEGGER_RTT_vprintf(0, format, &args);
    va_end(args);
    return ret;
}

bool console_prompt_u32(const char *prompt, uint32_t *out) {
    return prompt_rtt_u32(prompt, out);
}

void console_prompt(void) {
    console_puts("rtt> ");
}

bool console_poll_line(char *buf, size_t buf_size, size_t *len) {
    int key = 0;

    if (buf == NULL || len == NULL || buf_size == 0u) {
        return false;
    }

    key = SEGGER_RTT_GetKey();
    if (key < 0) {
        return false;
    }

    if (key == '\r' || key == '\n') {
        console_puts("\r\n");
        buf[*len] = '\0';
        return true;
    }

    if ((key == '\b' || key == 0x7F) && *len > 0u) {
        (*len)--;
        console_puts("\b \b");
        return false;
    }

    if (key >= 0x20 && key <= 0x7E && *len < (buf_size - 1u)) {
        buf[*len] = (char)key;
        (*len)++;
        SEGGER_RTT_PutChar(0, (char)key);
    }

    return false;
}

static int read_key_blocking(void) {
    int key = -1;

    do {
        key = SEGGER_RTT_GetKey();
    } while (key < 0);

    return key;
}

static bool parse_u32_string(const char *text, uint32_t *value) {
    uint32_t parsed = 0u;
    uint32_t base = 10u;
    size_t start = 0u;

    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16u;
        start = 2u;
        if (text[start] == '\0') {
            return false;
        }
    }

    for (size_t i = start; text[i] != '\0'; ++i) {
        uint32_t digit = 0u;

        if (text[i] >= '0' && text[i] <= '9') {
            digit = (uint32_t)(text[i] - '0');
        } else if (base == 16u && text[i] >= 'a' && text[i] <= 'f') {
            digit = (uint32_t)(text[i] - 'a' + 10);
        } else if (base == 16u && text[i] >= 'A' && text[i] <= 'F') {
            digit = (uint32_t)(text[i] - 'A' + 10);
        } else {
            return false;
        }

        parsed = parsed * base + digit;
    }

    *value = parsed;
    return true;
}

bool prompt_rtt_u32(const char *prompt, uint32_t *out) {
    char buf[24];
    size_t len = 0u;

    if (prompt == NULL || out == NULL) {
        return false;
    }

    console_puts(prompt);

    while (len < (sizeof(buf) - 1u)) {
        int key = read_key_blocking();

        if (key == '\r' || key == '\n') {
            if (len == 0u) {
                continue;
            }
            break;
        }

        if (key == '\b' || key == 0x7F) {
            if (len > 0u) {
                len--;
                console_puts("\b \b");
            }
            continue;
        }

        buf[len] = (char)key;
        len++;
        SEGGER_RTT_PutChar(0, (char)key);
    }

    buf[len] = '\0';
    console_puts("\r\n");
    return parse_u32_string(buf, out);
}

static const char *yes_no(bool value) {
    return value ? "yes" : "no";
}

static bool parse_scheme_name(const char *text, patch_scheme_t *scheme) {
    if (text == NULL || scheme == NULL) {
        return false;
    }

    if (strcmp(text, "legacy") == 0 || strcmp(text, "clearbitpatch") == 0) {
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

static void print_exec_result(const char *stage, int ret_code) {
    SEGGER_RTT_printf(0,
        "[result] %-8s ret=%d (%s) fixed=%s\r\n",
        stage,
        ret_code,
        patch_result_name(ret_code),
        yes_no(patch_result_is_fixed(ret_code)));
}

static void run_demo_for_scheme(patch_scheme_t scheme) {
    if (!patch_demo_can_run(scheme)) {
        SEGGER_RTT_printf(0,
            "[-] %s demo requires a pristine flash image. Reflash/reset before rerunning it.\r\n",
            patch_scheme_name(scheme));
        return;
    }

    prepare_scheme_baseline(scheme);
    app_set_exec_mode(APP_EXEC_MODE_DEMO);

    SEGGER_RTT_printf(0,
        "\r\n--- Demo Start [%s / %s] ---\r\n",
        patch_scheme_name(scheme),
        cve_target_name(cve_target_get_current()));

    console_puts("1. Initial call (baseline vulnerable path):\r\n");
    print_exec_result("baseline", patch_call(scheme));

    console_puts("\r\n2. Applying patch...\r\n");
    if (!patch_apply(scheme)) {
        SEGGER_RTT_printf(0, "[-] %s patch apply failed.\r\n", patch_scheme_name(scheme));
    }
    print_patch_status(scheme);

    console_puts("3. Call after patch (same crafted attack):\r\n");
    print_exec_result("patched", patch_call(scheme));

    console_puts("\r\n4. Removing patch...\r\n");
    patch_unapply(scheme);
    print_patch_status(scheme);

    console_puts("5. Call after unpatch (same crafted attack):\r\n");
    print_exec_result("unfixed", patch_call(scheme));

    app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
    SEGGER_RTT_printf(0,
        "--- Demo End [%s / %s] ---\r\n",
        patch_scheme_name(scheme),
        cve_target_name(cve_target_get_current()));
}

void console_print_help(void) {
    console_puts(
        "commands: help, mode legacy|clearbitpatch|rapid|hera|autopatch, "
        "target cve2024-2212|cve2025-1674|cve2025-12899, demo, bench, compare, call, patch, unpatch, status\r\n");
}

void console_print_status(void) {
    print_mode_line();
    cve_target_print_status();
    print_all_patch_status();
}

void console_run_startup_smoke_test(void) {
#if APP_STARTUP_SMOKE_TEST
    console_puts("\r\n=== Startup Smoke Test ===\r\n");
    run_demo_for_scheme(PATCH_SCHEME_AUTOPATCH);
    benchmark_compare_and_print();
#endif
}

void console_handle_command(const char *cmd) {
    patch_scheme_t scheme = PATCH_SCHEME_RAPID;
    cve_target_t target = CVE_TARGET_CVE2024_2212;

    if (cmd == NULL || cmd[0] == '\0') {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        console_print_help();
        return;
    }

    if (strcmp(cmd, "demo") == 0) {
        run_demo_for_scheme(g_current_scheme);
        return;
    }

    if (strcmp(cmd, "bench") == 0) {
        benchmark_run_and_print(g_current_scheme);
        return;
    }

    if (strcmp(cmd, "compare") == 0) {
        benchmark_compare_and_print();
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
        console_print_status();
        return;
    }

    if (strcmp(cmd, "target") == 0) {
        cve_target_print_status();
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

    if (strncmp(cmd, "target ", 7) == 0) {
        if (!cve_target_parse_name(cmd + 7, &target)) {
            SEGGER_RTT_printf(0, "[-] unknown target: %s\r\n", cmd + 7);
            return;
        }
        cve_target_set_current(target);
        cve_target_print_status();
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
        benchmark_run_and_print(scheme);
        return;
    }

    SEGGER_RTT_printf(0, "unknown command: %s\r\n", cmd);
}
