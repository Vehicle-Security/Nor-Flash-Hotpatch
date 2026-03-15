#include "hera_patch.h"

#include <string.h>

#include "queue_demo.h"

typedef struct {
    bool active;
    uint32_t control_word;
    uintptr_t payload_addr;
} hera_patch_context_t;

static hera_patch_context_t g_hera_ctx = {0};

static const queue_demo_profile_t g_hera_profile = {
    .banner = "\r\n=== [HERA] RAM Hotpatch Function ===\r\n",
    .status_line = "Status: RAM hotpatch validation ENABLED.\r\n",
    .reject_prefix = "HERA",
    .reject_wrap_line = "[HERA] Prevented integer wraparound before memory allocation!\r\n",
    .reject_abort_line = "[HERA] Queue creation aborted safely. Returning to shell...\r\n",
    .done_line = "\r\n[*] HERA patched path finished. Returning to shell...\r\n",
    .validate_before_alloc = true,
};

static const queue_demo_profile_t g_hera_fallback_profile = {
    .banner = "\r\n=== [HERA] Patch Inactive Fallback ===\r\n",
    .status_line = "Status: HERA inactive. Original vulnerable body is executing.\r\n",
    .reject_prefix = "",
    .reject_wrap_line = "",
    .reject_abort_line = "",
    .done_line = "\r\n[*] Vulnerable path finished. Returning to shell...\r\n",
    .validate_before_alloc = false,
};

static __attribute__((noinline, section(".text.hera_patch")))
int hera_hotfix_payload(UBaseType_t uxQueueLength, UBaseType_t uxItemSize, bool verbose) {
    return queue_demo_run(uxQueueLength, uxItemSize, verbose, &g_hera_profile);
}

bool hera_patch_install(void) {
    g_hera_ctx.payload_addr = (uintptr_t)hera_hotfix_payload;
    g_hera_ctx.control_word = 0x48455241u;
    __DSB();
    __ISB();
    g_hera_ctx.active = true;
    return true;
}

void hera_patch_unapply(void) {
    memset(&g_hera_ctx, 0, sizeof(g_hera_ctx));
    __DSB();
    __ISB();
}

bool hera_patch_is_active(void) {
    return g_hera_ctx.active;
}

uintptr_t hera_patch_payload_addr(void) {
    return g_hera_ctx.payload_addr;
}

int hera_patch_slot(void) {
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

    if (!g_hera_ctx.active) {
        return queue_demo_run(uxQueueLength, uxItemSize, verbose, &g_hera_fallback_profile);
    }

    return hera_hotfix_payload(uxQueueLength, uxItemSize, verbose);
}
