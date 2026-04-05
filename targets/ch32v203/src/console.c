#include "console.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_mode.h"
#include "benchmark.h"
#include "board_port.h"
#include "cve_target.h"
#include "patch_control.h"
#include "patch_result.h"

#ifndef APP_STARTUP_SMOKE_TEST
#define APP_STARTUP_SMOKE_TEST 0
#endif

static bool parse_u32_string(const char *text, uint32_t *value)
{
    unsigned long parsed = 0ul;
    char *end = NULL;

    if ((text == NULL) || (value == NULL) || (*text == '\0')) {
        return false;
    }

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if ((errno != 0) || (end == text) || (*end != '\0') || (parsed > UINT32_MAX)) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool read_line_blocking(const char *prompt, char *buf, size_t buf_size)
{
    size_t len = 0u;
    bool saw_cr = false;

    if ((buf == NULL) || (buf_size < 2u)) {
        return false;
    }

    if (prompt != NULL) {
        console_puts(prompt);
    }

    for (;;) {
        int key = board_console_getchar_nonblock();

        if (key < 0) {
            continue;
        }

        if (saw_cr && (key == '\n')) {
            saw_cr = false;
            continue;
        }

        if ((key == '\r') || (key == '\n')) {
            saw_cr = (key == '\r');
            console_puts("\r\n");
            buf[len] = '\0';
            return true;
        }

        if ((key == '\b') || (key == 0x7F)) {
            if (len > 0u) {
                --len;
                console_puts("\b \b");
            }
            continue;
        }

        if (isprint((unsigned char)key) && ((len + 1u) < buf_size)) {
            buf[len++] = (char)key;
            board_console_putchar((char)key);
        }
    }
}

static const char *yes_no(bool value)
{
    return value ? "yes" : "no";
}

static void print_exec_result(const char *stage, int ret_code)
{
    console_printf(
        "[result] %-8s ret=%d (%s) fixed=%s\r\n",
        stage,
        ret_code,
        patch_result_name(ret_code),
        yes_no(patch_result_is_fixed(ret_code)));
}

static void run_demo(void)
{
    if (!patch_demo_can_run()) {
        console_printf(
            "[-] %s demo requires a pristine flash image. Reset/reflash before rerunning.\r\n",
            patch_scheme_name());
        return;
    }

    app_set_exec_mode(APP_EXEC_MODE_DEMO);

    console_printf(
        "\r\n--- Demo Start [%s / %s] ---\r\n",
        patch_scheme_name(),
        cve_target_name(cve_target_get_current()));

    console_puts("1. Initial call (baseline vulnerable path):\r\n");
    print_exec_result("baseline", patch_call());

    console_puts("\r\n2. Applying patch...\r\n");
    if (!patch_apply()) {
        console_printf("[-] %s patch apply failed.\r\n", patch_scheme_name());
    }
    print_patch_status();

    console_puts("3. Call after patch (same crafted attack):\r\n");
    print_exec_result("patched", patch_call());

    console_puts("\r\n4. Removing patch...\r\n");
    patch_unapply();
    print_patch_status();

    console_puts("5. Call after unpatch (same crafted attack):\r\n");
    print_exec_result("unfixed", patch_call());

    app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
    console_printf(
        "--- Demo End [%s / %s] ---\r\n",
        patch_scheme_name(),
        cve_target_name(cve_target_get_current()));
}

void console_init(void)
{
    (void)board_console_init();
    console_puts("\r\nUART console ready. (ClearBitPatch-RV Demo)\r\n");
}

void console_puts(const char *s)
{
    board_console_write(s);
}

void console_printf(const char *fmt, ...)
{
    char buffer[256];
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written <= 0) {
        return;
    }

    buffer[sizeof(buffer) - 1u] = '\0';
    console_puts(buffer);
}

void console_prompt(void)
{
    console_puts("shell> ");
}

bool console_poll_line(char *buf, size_t buf_size, size_t *len)
{
    static bool saw_cr = false;
    int key = 0;

    if ((buf == NULL) || (len == NULL) || (buf_size == 0u)) {
        return false;
    }

    key = board_console_getchar_nonblock();
    if (key < 0) {
        return false;
    }

    if (saw_cr && (key == '\n')) {
        saw_cr = false;
        return false;
    }

    if ((key == '\r') || (key == '\n')) {
        saw_cr = (key == '\r');
        console_puts("\r\n");
        buf[*len] = '\0';
        return true;
    }

    if (((key == '\b') || (key == 0x7F)) && (*len > 0u)) {
        (*len)--;
        console_puts("\b \b");
        return false;
    }

    if ((key >= 0x20) && (key <= 0x7E) && (*len < (buf_size - 1u))) {
        buf[*len] = (char)key;
        (*len)++;
        board_console_putchar((char)key);
    }

    return false;
}

bool console_prompt_u32(const char *prompt, uint32_t *out)
{
    char buf[24];

    if ((prompt == NULL) || (out == NULL)) {
        return false;
    }

    if (!read_line_blocking(prompt, buf, sizeof(buf))) {
        return false;
    }

    return parse_u32_string(buf, out);
}

void console_print_help(void)
{
    console_puts(
        "commands: help, target, call, patch, unpatch, status, demo, bench, benchcmp\r\n"
        "target names: cve2024-2212 | cve2025-1674 | cve2025-12899\r\n");
}

void console_print_status(void)
{
    console_printf("[scheme] %s\r\n", patch_scheme_name());
    cve_target_print_status();
    print_patch_status();

    if (cve_target_get_current() == CVE_TARGET_CVE2024_2212) {
        const patch_scheme_ops_t *scheme = patch_scheme_cve2024_2212_direct();

        if ((scheme != NULL) && (scheme->print_status != NULL)) {
            console_printf("[scheme-2212] %s\r\n", scheme->name);
            scheme->print_status();
        }
    }
}

void console_run_startup_smoke_test(void)
{
#if APP_STARTUP_SMOKE_TEST
    console_puts("\r\n=== Startup Smoke Test ===\r\n");
    run_demo();
#endif
}

void console_handle_command(const char *cmd_line)
{
    cve_target_t target = CVE_TARGET_CVE2024_2212;
    char line[80];
    char *ctx = NULL;
    char *cmd = NULL;
    char *arg = NULL;

    if ((cmd_line == NULL) || (cmd_line[0] == '\0')) {
        return;
    }

    strncpy(line, cmd_line, sizeof(line) - 1u);
    line[sizeof(line) - 1u] = '\0';

    cmd = strtok_r(line, " \t", &ctx);
    if (cmd == NULL) {
        return;
    }

    arg = strtok_r(NULL, " \t", &ctx);

    if (strcmp(cmd, "help") == 0) {
        console_print_help();
        return;
    }

    if (strcmp(cmd, "demo") == 0) {
        run_demo();
        return;
    }

    if (strcmp(cmd, "bench") == 0) {
        benchmark_run_and_print();
        return;
    }

    if (strcmp(cmd, "benchcmp") == 0) {
        benchmark_compare_run_and_print();
        return;
    }

    if (strcmp(cmd, "call") == 0) {
        print_exec_result("call", patch_call());
        return;
    }

    if (strcmp(cmd, "patch") == 0) {
        if (!patch_apply()) {
            console_printf("[-] %s patch apply failed.\r\n", patch_scheme_name());
        }
        print_patch_status();
        return;
    }

    if (strcmp(cmd, "unpatch") == 0) {
        patch_unapply();
        print_patch_status();
        return;
    }

    if (strcmp(cmd, "status") == 0) {
        console_print_status();
        return;
    }

    if (strcmp(cmd, "target") == 0) {
        if (arg == NULL) {
            cve_target_print_status();
            return;
        }

        if (!cve_target_parse_name(arg, &target)) {
            console_printf("[-] unknown target: %s\r\n", arg);
            return;
        }

        cve_target_set_current(target);
        cve_target_print_status();
        return;
    }

    console_printf("unknown command: %s\r\n", cmd);
}
