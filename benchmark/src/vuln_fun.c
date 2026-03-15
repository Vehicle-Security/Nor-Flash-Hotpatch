#include "app_common.h"
#include "patch_control.h"
#include "patch_result.h"
#include "queue_demo.h"
#include "rapidpatch_vm.h"

static const queue_demo_profile_t g_unpatched_profile = {
    .banner = "\r\n=== [UNPATCHED] CVE-2024-2212 Target Function ===\r\n",
    .status_line = "Status: NO Integer Overflow Checks!\r\n",
    .reject_prefix = "",
    .reject_wrap_line = "",
    .reject_abort_line = "",
    .done_line = "\r\n[*] Vulnerable path finished. Returning to shell...\r\n",
    .validate_before_alloc = false,
};

int hera_ram_dispatcher(void);

static __attribute__((used)) int fun1_impl(void) {
    UBaseType_t uxQueueLength = 0;
    UBaseType_t uxItemSize = 0;
    bool verbose = app_exec_mode_is_verbose();

    if (app_fetch_auto_inputs(&uxQueueLength, &uxItemSize)) {
        if (verbose) {
            console_puts("[DEMO] Auto-fed triggering input values.\r\n");
        }
    } else {
        PROMPT_RTT_U32("Enter uxQueueLength: ", uxQueueLength);
        PROMPT_RTT_U32("Enter uxItemSize: ", uxItemSize);
    }

    return queue_demo_run(uxQueueLength, uxItemSize, verbose, &g_unpatched_profile);
}

__attribute__((naked, noinline, used, aligned(4), section(".text.fun1_entry")))
int fun1(void) {
    __asm volatile(
        ".thumb                    \n"
        "b.w   fun1_impl           \n"
        ".word hera_ram_dispatcher + 1 \n"
    );
}

int rapid_vuln_target(UBaseType_t uxQueueLength, UBaseType_t uxItemSize) {
    bool verbose = app_exec_mode_is_verbose();
    int ret_code = rapid_fixed_patch_point_invoke(
        (uint32_t)uxQueueLength,
        (uint32_t)uxItemSize,
        0u,
        0u);

    if (ret_code != (int)RAPIDPATCH_FIXED_OP_PASS) {
        return ret_code;
    }

    {
        queue_demo_profile_t rapid_profile = g_unpatched_profile;

        rapid_profile.banner = "\r\n=== [RAPIDPATCH] Fixed Patch Point Target ===\r\n";
        rapid_profile.status_line = "Status: Filter passed. Original vulnerable body is executing.\r\n";
        return queue_demo_run(uxQueueLength, uxItemSize, verbose, &rapid_profile);
    }
}
