#include "app_mode.h"
#include "console.h"
#include "cve_target.h"

static const cve_target_profile_t g_unpatched_profile = {
    .banner = "\r\n=== [UNPATCHED] Selected Vulnerability Entry ===\r\n",
    .status_line = "Status: running selected vulnerability without fix.\r\n",
    .reject_prefix = "",
    .block_line = "",
    .abort_line = "",
    .done_line = "\r\n[*] Unpatched path finished. Returning to shell...\r\n",
    .apply_fix = false,
};

static int fun1_impl(void)
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

    return cve_target_run(&input, verbose, &g_unpatched_profile);
}

__attribute__((noinline, used))
int fun1(void)
{
    return fun1_impl();
}
