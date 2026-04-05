#include "app_mode.h"
#include "console.h"
#include "cve_target.h"

static const cve_target_profile_t g_patch_profile = {
    .banner = "\r\n=== [PATCHED] Selected Vulnerability Fix ===\r\n",
    .status_line = "Status: selected vulnerability fix is ENABLED.\r\n",
    .reject_prefix = "ClearBitPatch-RV",
    .block_line = "[PATCH] Fixed code blocked the crafted attack input.\r\n",
    .abort_line = "[PATCH] Returning to shell without entering vulnerable effect.\r\n",
    .done_line = "\r\n[*] Patched path finished. Returning to shell...\r\n",
    .apply_fix = true,
};

static int fun2_impl(void)
{
    cve_target_input_t input = {0};
    bool auto_fed = false;
    bool verbose = app_exec_mode_is_verbose();

    if (!cve_target_fetch_inputs(&input, &auto_fed)) {
        console_puts("[-] Failed to collect input for selected vulnerability.\r\n");
        return -127;
    }

    if (verbose && auto_fed) {
        console_puts("[DEMO] Loaded attack input for selected vulnerability.\r\n");
    }

    return cve_target_run(&input, verbose, &g_patch_profile);
}

__attribute__((noinline, used))
int fun2(void)
{
    return fun2_impl();
}
