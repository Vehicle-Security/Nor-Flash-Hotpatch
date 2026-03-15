#include "autopatch_mode.h"
#include "autopatch_symbols.h"

#include "SEGGER_RTT.h"

#include "app_common.h"
#include "patch_result.h"
#include "queue_demo.h"

static const queue_demo_profile_t g_autopatch_background_profile = {
    .banner = "\r\n=== [AUTOPATCH] Queue Target (Disabled) ===\r\n",
    .status_line = "Status: Generated filter is present but disabled. Original vulnerable body is executing.\r\n",
    .reject_prefix = "",
    .reject_wrap_line = "",
    .reject_abort_line = "",
    .done_line = "\r\n[*] AutoPatch background path finished. Returning to shell...\r\n",
    .validate_before_alloc = false,
};

static const queue_demo_profile_t g_autopatch_pass_profile = {
    .banner = "\r\n=== [AUTOPATCH] Queue Target (Enabled) ===\r\n",
    .status_line = "Status: Generated filter allowed the request. Original vulnerable body is executing.\r\n",
    .reject_prefix = "AUTOPATCH",
    .reject_wrap_line = "[AUTOPATCH] Generated filter blocked the queue integer overflow before allocation!\r\n",
    .reject_abort_line = "[AUTOPATCH] Request rejected safely. Returning to shell...\r\n",
    .done_line = "\r\n[*] AutoPatch filtered path finished. Returning to shell...\r\n",
    .validate_before_alloc = false,
};

static void autopatch_print_inputs(bool verbose, UBaseType_t queue_length, UBaseType_t item_size) {
    if (!verbose) {
        return;
    }

    SEGGER_RTT_printf(0,
        "\r\n[Input]\r\n"
        "  uxQueueLength = 0x%08X\r\n"
        "  uxItemSize    = 0x%08X\r\n",
        (uint32_t)queue_length,
        (uint32_t)item_size);
}

static bool autopatch_fetch_inputs(UBaseType_t *queue_length, UBaseType_t *item_size) {
    if (app_fetch_auto_inputs(queue_length, item_size)) {
        return true;
    }

    PROMPT_RTT_U32("Enter uxQueueLength: ", *queue_length);
    PROMPT_RTT_U32("Enter uxItemSize: ", *item_size);
    return false;
}

static int autopatch_run_background(UBaseType_t queue_length, UBaseType_t item_size, bool verbose) {
    if (verbose) {
        console_puts(g_autopatch_background_profile.banner);
        console_puts(g_autopatch_background_profile.status_line);
    }

    autopatch_print_inputs(verbose, queue_length, item_size);
    return queue_demo_run(queue_length, item_size, verbose, &g_autopatch_background_profile);
}

static int autopatch_run_filtered(UBaseType_t queue_length, UBaseType_t item_size, bool verbose) {
    uint32_t op = AUTOPATCH_FILTER_PASS;
    int32_t ret_code = PATCH_RESULT_SAFE_NOOP;

    (void)autopatch_invoke_filter(queue_length, item_size, &op, &ret_code);

    if (verbose) {
        console_puts(g_autopatch_pass_profile.banner);
        console_puts("Status: Calling the offline-compiled AutoPatch queue filter.\r\n");
    }

    autopatch_print_inputs(verbose, queue_length, item_size);

    if (verbose) {
        SEGGER_RTT_printf(0,
            "\r\n[Filter]\r\n"
            "  op       = %u\r\n"
            "  ret_code = %d\r\n",
            op,
            (int)ret_code);
    }

    if (op == AUTOPATCH_FILTER_DROP || op == AUTOPATCH_FILTER_REDIRECT) {
        if (verbose && patch_result_is_fixed(ret_code)) {
            console_puts(g_autopatch_pass_profile.reject_wrap_line);
            console_puts(g_autopatch_pass_profile.reject_abort_line);
        }
        return ret_code;
    }

    return queue_demo_run(queue_length, item_size, verbose, &g_autopatch_pass_profile);
}

int autopatch_patch_slot(void) {
    UBaseType_t queue_length = 0;
    UBaseType_t item_size = 0;
    bool verbose = app_exec_mode_is_verbose();
    bool auto_fed = autopatch_fetch_inputs(&queue_length, &item_size);

    if (verbose) {
        if (auto_fed) {
            console_puts("[DEMO] Auto-fed triggering input values.\r\n");
        }
    }

    if (!autopatch_is_enabled()) {
        return autopatch_run_background(queue_length, item_size, verbose);
    }

    return autopatch_run_filtered(queue_length, item_size, verbose);
}

int autopatch_background_call(void) {
    const autopatch_input_t *input = autopatch_default_input();
    return autopatch_run_background(input->queue_length, input->item_size, app_exec_mode_is_verbose());
}

int autopatch_patched_call(void) {
    const autopatch_input_t *input = autopatch_default_input();
    return autopatch_run_filtered(input->queue_length, input->item_size, app_exec_mode_is_verbose());
}

void autopatch_print_status(void) {
    SEGGER_RTT_printf(0,
        "[autopatch] linked=yes ready=%s enabled=%s entry=0x%08X online_toggle=soft-switch\r\n",
        autopatch_is_ready() ? "yes" : "no",
        autopatch_is_enabled() ? "yes" : "no",
        (uint32_t)autopatch_filter_addr());
}
