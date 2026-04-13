/*
 * console.c — RTT-based interactive console for MorphPatch demo.
 *
 * Provides commands: help, target, call, patch, unpatch, status, demo, bench.
 * All I/O is routed through the platform_port abstraction so the same console
 * logic works on baremetal, FreeRTOS, LiteOS, and NuttX ports.
 */
#include "console.h"

#include <stdarg.h>
#include <string.h>

#include "../app/app_mode.h"
#include "../benchmark/benchmark.h"
#include "../patch/patch_control.h"
#include "../patch/morph_patch.h"
#include "../patch/patch_result.h"
#include "../target/cve_target.h"
#include "ports/common/platform_port.h"

#ifndef APP_STARTUP_SMOKE_TEST
#define APP_STARTUP_SMOKE_TEST 0
#endif

void console_init(void) {
    platform_console_init();
}

void console_puts(const char *s) {
    platform_console_write(s);
}

int console_printf(const char *format, ...) {
    int written = 0;
    va_list args;

    va_start(args, format);
    written = platform_console_vprintf(format, &args);
    va_end(args);

    return written;
}

void console_prompt(void) {
    platform_command_prompt();
}

bool console_poll_line(char *buf, size_t buf_size, size_t *len) {
    return platform_command_poll_line(buf, buf_size, len);
}

bool console_prompt_u32(const char *prompt, uint32_t *out) {
    return platform_command_prompt_u32(prompt, out);
}

static const char *yes_no(bool value) {
    return value ? "yes" : "no";
}

static bool parse_patch_path_name(const char *text, morph_patch_path_t *path) {
    if (text == NULL || path == NULL) {
        return false;
    }

    if (strcmp(text, "direct") == 0 || strcmp(text, "branch") == 0) {
        *path = MORPH_PATCH_PATH_DIRECT;
        return true;
    }

    if (strcmp(text, "fault") == 0 || strcmp(text, "exception") == 0) {
        *path = MORPH_PATCH_PATH_FAULT;
        return true;
    }

    return false;
}

static void print_exec_result(const char *stage, int ret_code) {
    console_printf(
        "[result] %-8s ret=%d (%s) fixed=%s\r\n",
        stage,
        ret_code,
        patch_result_name(ret_code),
        yes_no(patch_result_is_fixed(ret_code)));
}

static void run_demo(void) {
    if (!patch_demo_can_run()) {
        console_printf(
            "[-] %s demo requires a pristine flash image. Reflash/reset before rerunning it.\r\n",
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

void console_print_help(void) {
    console_puts(
        "commands: help, target, path direct|fault, call, patch, unpatch, status, demo, bench\r\n"
        "target names: cve2024-2212 | cve2025-1674 | cve2025-12899\r\n");
}

void console_print_status(void) {
    console_printf("[scheme] %s\r\n", patch_scheme_name());
    cve_target_print_status();
    print_patch_status();
}

void console_run_startup_smoke_test(void) {
#if APP_STARTUP_SMOKE_TEST
    console_puts("\r\n=== Startup Smoke Test ===\r\n");
    run_demo();
#endif
}

void console_handle_command(const char *cmd) {
    cve_target_t target = CVE_TARGET_CVE2024_2212;
    morph_patch_path_t path = MORPH_PATCH_PATH_DIRECT;

    if (cmd == NULL || cmd[0] == '\0') {
        return;
    }

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
        cve_target_print_status();
        return;
    }

    if (strcmp(cmd, "path") == 0) {
        console_printf("[path] %s\r\n", morph_patch_path_name());
        return;
    }

    if (strncmp(cmd, "target ", 7) == 0) {
        if (!cve_target_parse_name(cmd + 7, &target)) {
            console_printf("[-] unknown target: %s\r\n", cmd + 7);
            return;
        }
        cve_target_set_current(target);
        cve_target_print_status();
        return;
    }

    if (strncmp(cmd, "path ", 5) == 0) {
        if (!parse_patch_path_name(cmd + 5, &path)) {
            console_printf("[-] unknown path: %s\r\n", cmd + 5);
            return;
        }
        morph_patch_set_path(path);
        console_printf("[path] switched to %s\r\n", morph_patch_path_name());
        print_patch_status();
        return;
    }

    console_printf("unknown command: %s\r\n", cmd);
}
