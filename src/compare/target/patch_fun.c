#include "core/app/app_mode.h"
#include "compare/console/console.h"
#include "cve_target.h"

static const cve_target_profile_t g_legacy_profile = {
    .banner = "\r\n=== [LEGACY PATCHED] Selected Vulnerability Fix ===\r\n",
    .status_line = "Status: Replacement fix for the selected vulnerability is ENABLED.\r\n",
    .reject_prefix = "LEGACY PATCH",
    .block_line = "[LEGACY PATCH] Fixed code blocked the crafted attack input.\r\n",
    .abort_line = "[LEGACY PATCH] Returning to shell without entering the vulnerable path.\r\n",
    .done_line = "\r\n[*] Legacy patched path finished. Returning to shell...\r\n",
    .apply_fix = true,
};

static __attribute__((used)) int fun2_impl(void) {
    cve_target_input_t input = {0};
    bool auto_fed = false;
    bool verbose = app_exec_mode_is_verbose();

    if (!cve_target_fetch_inputs(&input, &auto_fed)) {
        console_puts("[-] Failed to collect input for the selected vulnerability.\r\n");
        return -127;
    }

    if (verbose && auto_fed) {
        console_puts("[DEMO] Loaded attack input for the selected vulnerability.\r\n");
    }

    return cve_target_run(&input, verbose, &g_legacy_profile);
}

__attribute__((naked, noinline, used, section(".hotpatch_page.entry"), aligned(2)))
int fun2(void) {
    __asm volatile(
        ".thumb        \n"
        "b.w fun2_impl  \n");
}
