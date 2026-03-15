#include "app_common.h"
#include "patch_control.h"
#include "queue_demo.h"

static const queue_demo_profile_t g_legacy_profile = {
    .banner = "\r\n=== [LEGACY PATCHED] Hotpatch Replacement Function ===\r\n",
    .status_line = "Status: Pre-allocation Validation ENABLED.\r\n",
    .reject_prefix = "HOTPATCH",
    .reject_wrap_line = "[HOTPATCH] Prevented integer wraparound before memory allocation!\r\n",
    .reject_abort_line = "[HOTPATCH] Queue creation aborted safely. Returning to shell...\r\n",
    .done_line = "\r\n[*] fun2 finished. Returning to shell...\r\n",
    .validate_before_alloc = true,
};

static __attribute__((used)) int fun2_impl(void) {
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

    return queue_demo_run(uxQueueLength, uxItemSize, verbose, &g_legacy_profile);
}

__attribute__((naked, noinline, used, section(".hotpatch_page.entry"), aligned(2)))
int fun2(void) {
    __asm volatile(
        ".thumb        \n"
        "b.w fun2_impl  \n"
    );
}

static const uint8_t g_rapid_patch_code[] = ""
"\xb7\x00\x00\x00\xff\xff\xff\xff\x61\x13\x00\x00\x00\x00\x00\x00\x18\x02\x00\x00\x00\x00\x00\x00\x00"
"\x00\x00\x00\x01\x00\x00\x00\x15\x03\x0b\x00\x00\x00\x00\x00\x61\x11\x04\x00\x00\x00\x00\x00\xb7\x00"
"\x00\x00\xfe\xff\xff\xff\x15\x01\x08\x00\x00\x00\x00\x00\x2f\x31\x00\x00\x00\x00\x00\x00\xb7\x02\x00"
"\x00\x00\x00\x00\x00\x77\x01\x00\x00\x20\x00\x00\x00\xb7\x00\x00\x00\x00\x00\x00\x00\x15\x01\x03\x00"
"\x00\x00\x00\x00\xb7\x00\x00\x00\xfd\xff\xff\xff\x18\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"
"\x00\x00\x00\x0f\x20\x00\x00\x00\x00\x00\x00\x95\x00\x00\x00\x00\x00\x00\x00";

const uint8_t *rapid_patch_code_bytes(void) {
    return g_rapid_patch_code;
}

uint16_t rapid_patch_code_size(void) {
    return (uint16_t)(sizeof(g_rapid_patch_code) - 1u);
}
