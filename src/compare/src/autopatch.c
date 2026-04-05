/*
 * autopatch.c — AutoPatch comparison implementation.
 *
 * Emulates an automatic patch system using static trampoline-based dispatch.
 * A binary-search dispatcher maps vulnerability site IDs to patch descriptors.
 * Each patch body is a pre-compiled native function pointer that replaces the
 * original execution at the instrumented call site.
 *
 * NOTE: This is a simplified runtime-only model.  The real AutoPatch pipeline
 * includes offline LLVM analysis and patch generation, which are not measured.
 */
#include "compare.h"

#include <stddef.h>
#include <string.h>

#include "SEGGER_RTT.h"

#include "core/app/app_mode.h"
#include "core/console/console.h"
#include "core/platform/cycle_counter.h"
#include "core/patch/patch_result.h"
#include "core/target/cve_target.h"
#include "nrf.h"

enum {
    AUTOPATCH_RUNTIME_PASS = 0x00010000u,
    AUTOPATCH_MAX_PATCHES = 64u,
    AUTOPATCH_ACTION_REPLACE = 3u,
};

typedef enum {
    AUTOPATCH_SITE_FUNC_ENTRY = 1u,
    AUTOPATCH_SITE_AFTER_CALL = 2u,
    AUTOPATCH_SITE_AFTER_IF = 3u,
    AUTOPATCH_SITE_AFTER_COMPLEX_LOOP = 4u,
} autopatch_site_t;

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} autopatch_stack_frame_t;

typedef struct {
    uint32_t target;
    uint32_t site_id;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
} autopatch_patch_ctx_t;

typedef uint64_t (*autopatch_handler_fn_t)(const autopatch_patch_ctx_t *ctx);

typedef struct {
    uint32_t site_id;
    uint32_t target_id;
    autopatch_handler_fn_t handler_fn;
    const char *handler_name;
    bool active;
} autopatch_patch_desc_t;

typedef struct {
    bool active;
    uint32_t patch_count;
    autopatch_patch_desc_t patch_table[AUTOPATCH_MAX_PATCHES];
} autopatch_runtime_t;

static autopatch_runtime_t g_autopatch_rt = {0};

static const cve_target_profile_t g_autopatch_background_profile = {
    .banner = "\r\n=== [AUTOPATCH] Selected Vulnerability (Disabled) ===\r\n",
    .status_line = "Status: static trampoline is present but patch is disabled.\r\n",
    .reject_prefix = "",
    .block_line = "",
    .abort_line = "",
    .done_line = "\r\n[*] AutoPatch background path finished. Returning to shell...\r\n",
    .apply_fix = false,
};

static const cve_target_profile_t g_autopatch_filter_profile = {
    .banner = "\r\n=== [AUTOPATCH] Selected Vulnerability (Enabled) ===\r\n",
    .status_line = "Status: trampoline -> dispatcher -> hotpatch body is active.\r\n",
    .reject_prefix = "AUTOPATCH",
    .block_line = "[AUTOPATCH] Static hotpatch body blocked the crafted attack input.\r\n",
    .abort_line = "[AUTOPATCH] Returning to shell without entering the vulnerable path.\r\n",
    .done_line = "",
    .apply_fix = false,
};

static const char *autopatch_site_name(uint32_t site_id) {
    if (site_id == AUTOPATCH_SITE_FUNC_ENTRY) {
        return "func-entry";
    }
    if (site_id == AUTOPATCH_SITE_AFTER_CALL) {
        return "after-call";
    }
    if (site_id == AUTOPATCH_SITE_AFTER_IF) {
        return "after-if";
    }
    if (site_id == AUTOPATCH_SITE_AFTER_COMPLEX_LOOP) {
        return "after-complex-loop";
    }
    return "unknown";
}

static uint64_t autopatch_encode_result(uint32_t op, int32_t ret_code) {
    return ((uint64_t)op << 32) | (uint32_t)ret_code;
}

static int autopatch_decode_result(uint64_t raw_ret) {
    uint32_t op = (uint32_t)(raw_ret >> 32);
    int32_t ret_code = (int32_t)(uint32_t)raw_ret;

    if (op == CVE_FILTER_PASS) {
        return (int)AUTOPATCH_RUNTIME_PASS;
    }

    if (op == CVE_FILTER_DROP || op == CVE_FILTER_REDIRECT || op == AUTOPATCH_ACTION_REPLACE) {
        return ret_code;
    }

    return -127;
}

/*
 * AutoPatch runtime model in compare:
 * - static trampoline captures runtime live context into arg0..arg3
 * - dispatcher selects per-target hotpatch handler
 * - this excludes offline LLVM auto-generation and only measures runtime path
 */
static uint64_t autopatch_hotpatch_cve2024_2212(const autopatch_patch_ctx_t *ctx) {
    uint32_t queue_length = 0u;
    uint32_t item_size = 0u;

    if (ctx == NULL) {
        return autopatch_encode_result(CVE_FILTER_DROP, -127);
    }

    queue_length = ctx->arg0;
    item_size = ctx->arg1;

    if (queue_length == 0u) {
        return autopatch_encode_result(CVE_FILTER_DROP, -1);
    }
    if (item_size == 0u) {
        return autopatch_encode_result(CVE_FILTER_DROP, -2);
    }
    if (queue_length > (0xFFFFFFFFu / item_size)) {
        return autopatch_encode_result(CVE_FILTER_DROP, PATCH_RESULT_ATTACK_BLOCKED);
    }

    return autopatch_encode_result(CVE_FILTER_PASS, PATCH_RESULT_SAFE_NOOP);
}

static uint64_t autopatch_hotpatch_cve2025_1674(const autopatch_patch_ctx_t *ctx) {
    uint32_t answer_offset = 0u;
    uint32_t msg_size = 0u;
    uint32_t dname_len = 0u;
    uint32_t rem_size = 0u;
    uint32_t computed_rem = 0u;

    if (ctx == NULL) {
        return autopatch_encode_result(CVE_FILTER_DROP, -127);
    }

    /* live runtime context: arg0=answer_offset, arg1=msg_size, arg2=dname_len, arg3=runtime_rem_size */
    answer_offset = ctx->arg0;
    msg_size = ctx->arg1;
    dname_len = ctx->arg2;
    rem_size = ctx->arg3;

    if (answer_offset >= msg_size) {
        return autopatch_encode_result(CVE_FILTER_DROP, PATCH_RESULT_REJECT_DNS_BOUNDS);
    }
    if (dname_len > (msg_size - answer_offset)) {
        return autopatch_encode_result(CVE_FILTER_DROP, PATCH_RESULT_REJECT_DNS_BOUNDS);
    }

    computed_rem = msg_size - answer_offset - dname_len;
    if (computed_rem != rem_size) {
        return autopatch_encode_result(CVE_FILTER_DROP, PATCH_RESULT_REJECT_DNS_BOUNDS);
    }
    if (rem_size < 10u) {
        return autopatch_encode_result(CVE_FILTER_DROP, PATCH_RESULT_REJECT_DNS_BOUNDS);
    }

    return autopatch_encode_result(CVE_FILTER_PASS, PATCH_RESULT_SAFE_NOOP);
}

static uint64_t autopatch_hotpatch_cve2025_12899(const autopatch_patch_ctx_t *ctx) {
    uint32_t pkt_family = 0u;
    uint32_t handler_family = 0u;
    uint32_t icmp_type = 0u;
    uint32_t icmp_code = 0u;

    if (ctx == NULL) {
        return autopatch_encode_result(CVE_FILTER_DROP, -127);
    }

    /* live runtime context: arg0=pkt_family, arg1=registered_handler_family, arg2=icmp_type, arg3=icmp_code */
    pkt_family = ctx->arg0;
    handler_family = ctx->arg1;
    icmp_type = ctx->arg2;
    icmp_code = ctx->arg3;

    if (icmp_type == 128u && icmp_code == 0u && pkt_family != handler_family) {
        return autopatch_encode_result(CVE_FILTER_DROP, PATCH_RESULT_REJECT_ICMP_FAMILY);
    }

    return autopatch_encode_result(CVE_FILTER_PASS, PATCH_RESULT_SAFE_NOOP);
}

static autopatch_handler_fn_t autopatch_handler_for_target(uint32_t target_id) {
    if (target_id == (uint32_t)CVE_TARGET_CVE2024_2212) {
        return autopatch_hotpatch_cve2024_2212;
    }
    if (target_id == (uint32_t)CVE_TARGET_CVE2025_1674) {
        return autopatch_hotpatch_cve2025_1674;
    }
    if (target_id == (uint32_t)CVE_TARGET_CVE2025_12899) {
        return autopatch_hotpatch_cve2025_12899;
    }
    return autopatch_hotpatch_cve2024_2212;
}

static const char *autopatch_handler_name_for_target(uint32_t target_id) {
    if (target_id == (uint32_t)CVE_TARGET_CVE2024_2212) {
        return "hotpatch_cve2024_2212";
    }
    if (target_id == (uint32_t)CVE_TARGET_CVE2025_1674) {
        return "hotpatch_cve2025_1674";
    }
    if (target_id == (uint32_t)CVE_TARGET_CVE2025_12899) {
        return "hotpatch_cve2025_12899";
    }
    return "hotpatch_unknown";
}

static int autopatch_desc_key_cmp(uint32_t target_id,
                                  uint32_t site_id,
                                  const autopatch_patch_desc_t *desc) {
    if (desc == NULL) {
        return 1;
    }

    if (target_id < desc->target_id) {
        return -1;
    }
    if (target_id > desc->target_id) {
        return 1;
    }
    if (site_id < desc->site_id) {
        return -1;
    }
    if (site_id > desc->site_id) {
        return 1;
    }
    return 0;
}

static bool autopatch_patch_table_insert_sorted(uint32_t target_id,
                                                uint32_t site_id,
                                                autopatch_handler_fn_t handler_fn,
                                                const char *handler_name) {
    uint32_t insert_idx = 0u;

    if (handler_fn == NULL || g_autopatch_rt.patch_count >= AUTOPATCH_MAX_PATCHES) {
        return false;
    }

    while (insert_idx < g_autopatch_rt.patch_count) {
        int cmp = autopatch_desc_key_cmp(target_id, site_id, &g_autopatch_rt.patch_table[insert_idx]);
        if (cmp <= 0) {
            break;
        }
        insert_idx++;
    }

    if (insert_idx < g_autopatch_rt.patch_count &&
        autopatch_desc_key_cmp(target_id, site_id, &g_autopatch_rt.patch_table[insert_idx]) == 0) {
        g_autopatch_rt.patch_table[insert_idx].handler_fn = handler_fn;
        g_autopatch_rt.patch_table[insert_idx].handler_name = handler_name;
        g_autopatch_rt.patch_table[insert_idx].active = true;
        return true;
    }

    for (uint32_t i = g_autopatch_rt.patch_count; i > insert_idx; --i) {
        g_autopatch_rt.patch_table[i] = g_autopatch_rt.patch_table[i - 1u];
    }

    g_autopatch_rt.patch_table[insert_idx].target_id = target_id;
    g_autopatch_rt.patch_table[insert_idx].site_id = site_id;
    g_autopatch_rt.patch_table[insert_idx].handler_fn = handler_fn;
    g_autopatch_rt.patch_table[insert_idx].handler_name = handler_name;
    g_autopatch_rt.patch_table[insert_idx].active = true;
    g_autopatch_rt.patch_count++;
    return true;
}

static const autopatch_patch_desc_t *autopatch_dispatcher_lookup_desc(uint32_t target_id, uint32_t site_id) {
    int32_t left = 0;
    int32_t right = 0;

    if (!g_autopatch_rt.active || g_autopatch_rt.patch_count == 0u) {
        return NULL;
    }

    right = (int32_t)g_autopatch_rt.patch_count - 1;
    while (left <= right) {
        int32_t mid = left + ((right - left) / 2);
        autopatch_patch_desc_t *desc = &g_autopatch_rt.patch_table[mid];
        int cmp = autopatch_desc_key_cmp(target_id, site_id, desc);

        if (cmp == 0) {
            return desc->active ? desc : NULL;
        }

        if (cmp < 0) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }

    return NULL;
}

static autopatch_handler_fn_t autopatch_dispatcher_lookup(uint32_t target_id, uint32_t site_id) {
    const autopatch_patch_desc_t *desc = autopatch_dispatcher_lookup_desc(target_id, site_id);

    if (desc == NULL) {
        return NULL;
    }
    return desc->handler_fn;
}

static void autopatch_prepare_frame(autopatch_stack_frame_t *frame,
                                    uint32_t r0,
                                    uint32_t r1,
                                    uint32_t r2,
                                    uint32_t r3) {
    if (frame == NULL) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->r0 = r0;
    frame->r1 = r1;
    frame->r2 = r2;
    frame->r3 = r3;
}

static void autopatch_fill_ctx_for_target(uint32_t target,
                                          uint32_t site_id,
                                          uint32_t r0,
                                          uint32_t r1,
                                          uint32_t r2,
                                          uint32_t r3,
                                          autopatch_patch_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->target = target;
    ctx->site_id = site_id;

    /*
     * Use r0..r3 as logical runtime slots for all targets.
     * 2024-2212: arg0=queue_length, arg1=item_size
     * 2025-1674: arg0=answer_offset, arg1=msg_size, arg2=dname_len, arg3=rem_size
     * 2025-12899: arg0=pkt_family, arg1=handler_family, arg2=icmp_type, arg3=icmp_code
     */
    if (target == (uint32_t)CVE_TARGET_CVE2024_2212) {
        ctx->arg0 = r0;
        ctx->arg1 = r1;
        ctx->arg2 = r2;
        ctx->arg3 = r3;
        return;
    }

    if (target == (uint32_t)CVE_TARGET_CVE2025_1674) {
        ctx->arg0 = r0;
        ctx->arg1 = r1;
        ctx->arg2 = r2;
        ctx->arg3 = r3;
        return;
    }

    if (target == (uint32_t)CVE_TARGET_CVE2025_12899) {
        ctx->arg0 = r0;
        ctx->arg1 = r1;
        ctx->arg2 = r2;
        ctx->arg3 = r3;
        return;
    }

    ctx->arg0 = r0;
    ctx->arg1 = r1;
    ctx->arg2 = r2;
    ctx->arg3 = r3;
}

static autopatch_patch_ctx_t autopatch_prepare_ctx(uint32_t site_id,
                                                   const autopatch_stack_frame_t *frame) {
    autopatch_patch_ctx_t ctx = {0};
    uint32_t target = (uint32_t)cve_target_get_current();
    uint32_t r0 = 0u;
    uint32_t r1 = 0u;
    uint32_t r2 = 0u;
    uint32_t r3 = 0u;

    if (frame != NULL) {
        r0 = frame->r0;
        r1 = frame->r1;
        r2 = frame->r2;
        r3 = frame->r3;
    }

    autopatch_fill_ctx_for_target(target, site_id, r0, r1, r2, r3, &ctx);
    return ctx;
}

static void autopatch_prepare_runtime_args_for_target(cve_target_t target,
                                                      uint32_t *r0,
                                                      uint32_t *r1,
                                                      uint32_t *r2,
                                                      uint32_t *r3) {
    if (r0 == NULL || r1 == NULL || r2 == NULL || r3 == NULL) {
        return;
    }

    if (target == CVE_TARGET_CVE2025_1674) {
        *r0 = 12u;
        *r1 = 18u;
        *r2 = 3u;
        *r3 = 3u;
        return;
    }

    if (target == CVE_TARGET_CVE2025_12899) {
        *r0 = 4u;
        *r1 = 6u;
        *r2 = 128u;
        *r3 = 0u;
    }
}

static int autopatch_trampoline_trigger_only(uint32_t site_id, const autopatch_stack_frame_t *frame) {
    autopatch_stack_frame_t local_frame = {0};
    autopatch_patch_ctx_t ctx = {0};
    volatile uint32_t sink = 0u;

    if (!g_autopatch_rt.active) {
        return (int)AUTOPATCH_RUNTIME_PASS;
    }

    if (frame != NULL) {
        local_frame = *frame;
    }
    local_frame.lr = site_id;
    ctx = autopatch_prepare_ctx(site_id, &local_frame);
    sink = ctx.arg0 ^ ctx.arg1 ^ ctx.arg2 ^ ctx.arg3 ^ ctx.site_id;
    (void)sink;
    return (int)AUTOPATCH_RUNTIME_PASS;
}

int autopatch_trampoline_invoke(uint32_t site_id, autopatch_stack_frame_t *frame) {
    autopatch_stack_frame_t local_frame = {0};
    autopatch_patch_ctx_t patch_ctx = {0};
    autopatch_handler_fn_t handler = NULL;
    uint64_t raw_ret = 0u;

    if (!g_autopatch_rt.active) {
        return (int)AUTOPATCH_RUNTIME_PASS;
    }

    if (frame != NULL) {
        local_frame = *frame;
    }
    local_frame.lr = site_id;

    patch_ctx = autopatch_prepare_ctx(site_id, &local_frame);
    handler = autopatch_dispatcher_lookup(patch_ctx.target, patch_ctx.site_id);
    if (handler == NULL) {
        return (int)AUTOPATCH_RUNTIME_PASS;
    }

    raw_ret = handler(&patch_ctx);
    return autopatch_decode_result(raw_ret);
}

static cve_target_input_t autopatch_prepare_input(cve_target_t target,
                                                  uint32_t r0,
                                                  uint32_t r1,
                                                  uint32_t r2,
                                                  uint32_t r3) {
    cve_target_input_t input = {0};

    cve_target_get_attack_inputs(&input);
    input.target = target;
    if (target == CVE_TARGET_CVE2024_2212) {
        input.queue_length = r0;
        input.item_size = r1;
    } else if (target == CVE_TARGET_CVE2025_1674) {
        input.runtime_arg0 = r0;
        input.runtime_arg1 = r1;
        input.runtime_arg2 = r2;
        input.runtime_arg3 = r3;
    } else if (target == CVE_TARGET_CVE2025_12899) {
        input.runtime_arg0 = r0;
        input.runtime_arg1 = r1;
        input.runtime_arg2 = r2;
        input.runtime_arg3 = r3;
    }

    return input;
}

static int autopatch_vuln_target(const cve_target_input_t *input,
                                 uint32_t r0,
                                 uint32_t r1,
                                 uint32_t r2,
                                 uint32_t r3) {
    autopatch_stack_frame_t frame = {0};
    bool verbose = app_exec_mode_is_verbose();
    int patch_ret = 0;

    if (input == NULL) {
        return -127;
    }

    autopatch_prepare_frame(&frame, r0, r1, r2, r3);
    patch_ret = autopatch_trampoline_invoke(AUTOPATCH_SITE_FUNC_ENTRY, &frame);
    if (patch_ret != (int)AUTOPATCH_RUNTIME_PASS) {
        return patch_ret;
    }

    return cve_target_run(input, verbose, &g_autopatch_background_profile);
}

bool autopatch_is_ready(void) {
    return true;
}

bool autopatch_patch_install(void) {
    if (!autopatch_is_ready()) {
        return false;
    }

    memset(&g_autopatch_rt, 0, sizeof(g_autopatch_rt));
    if (!autopatch_patch_table_insert_sorted(
            (uint32_t)CVE_TARGET_CVE2024_2212,
            AUTOPATCH_SITE_FUNC_ENTRY,
            autopatch_hotpatch_cve2024_2212,
            "hotpatch_cve2024_2212")) {
        return false;
    }
    if (!autopatch_patch_table_insert_sorted(
            (uint32_t)CVE_TARGET_CVE2025_1674,
            AUTOPATCH_SITE_FUNC_ENTRY,
            autopatch_hotpatch_cve2025_1674,
            "hotpatch_cve2025_1674")) {
        return false;
    }
    if (!autopatch_patch_table_insert_sorted(
            (uint32_t)CVE_TARGET_CVE2025_12899,
            AUTOPATCH_SITE_FUNC_ENTRY,
            autopatch_hotpatch_cve2025_12899,
            "hotpatch_cve2025_12899")) {
        return false;
    }

    g_autopatch_rt.active = true;
    __DSB();
    __ISB();
    return true;
}

void autopatch_patch_unapply(void) {
    memset(&g_autopatch_rt, 0, sizeof(g_autopatch_rt));
    __DSB();
    __ISB();
}

bool autopatch_patch_is_active(void) {
    return autopatch_is_ready() && g_autopatch_rt.active;
}

int autopatch_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    cve_target_t target = cve_target_get_current();
    cve_target_input_t input = autopatch_prepare_input(target, r0, r1, r2, r3);

    return autopatch_vuln_target(&input, r0, r1, r2, r3);
}

int autopatch_patch_slot(void) {
    cve_target_input_t input = {0};
    bool auto_fed = false;
    bool verbose = app_exec_mode_is_verbose();
    uint32_t r0 = 0u;
    uint32_t r1 = 0u;
    uint32_t r2 = 0u;
    uint32_t r3 = 0u;
    int ret = 0;

    if (!cve_target_fetch_inputs(&input, &auto_fed)) {
        console_puts("[-] Failed to collect input for the selected vulnerability.\r\n");
        return -127;
    }

    if (verbose && auto_fed) {
        console_puts("[DEMO] Loaded attack input for the selected vulnerability.\r\n");
    }

    r0 = (uint32_t)input.queue_length;
    r1 = (uint32_t)input.item_size;
    autopatch_prepare_runtime_args_for_target(input.target, &r0, &r1, &r2, &r3);

    ret = autopatch_invoke(r0, r1, r2, r3);
    if (verbose &&
        patch_result_is_fixed(ret) &&
        ret != PATCH_RESULT_SAFE_EXECUTED &&
        ret != PATCH_RESULT_SAFE_NOOP) {
        console_puts(g_autopatch_filter_profile.block_line);
        console_puts(g_autopatch_filter_profile.abort_line);
    }

    return ret;
}

static void autopatch_prepare_dispatch_bench_table(uint32_t active_patch_count) {
    uint32_t count = active_patch_count;
    uint32_t current_target = (uint32_t)cve_target_get_current();

    if (count == 0u) {
        count = 1u;
    }
    if (count > AUTOPATCH_MAX_PATCHES) {
        count = AUTOPATCH_MAX_PATCHES;
    }

    memset(&g_autopatch_rt, 0, sizeof(g_autopatch_rt));
    g_autopatch_rt.patch_count = count;
    g_autopatch_rt.active = true;

    g_autopatch_rt.patch_table[0].target_id = current_target;
    g_autopatch_rt.patch_table[0].site_id = AUTOPATCH_SITE_FUNC_ENTRY;
    g_autopatch_rt.patch_table[0].handler_fn = autopatch_handler_for_target(current_target);
    g_autopatch_rt.patch_table[0].handler_name = autopatch_handler_name_for_target(current_target);
    g_autopatch_rt.patch_table[0].active = true;

    for (uint32_t i = 1u; i < count; ++i) {
        g_autopatch_rt.patch_table[i].target_id = 1000u + i;
        g_autopatch_rt.patch_table[i].site_id = AUTOPATCH_SITE_FUNC_ENTRY;
        g_autopatch_rt.patch_table[i].handler_fn = autopatch_hotpatch_cve2024_2212;
        g_autopatch_rt.patch_table[i].handler_name = "dummy_hotpatch";
        g_autopatch_rt.patch_table[i].active = true;
    }
}

uint32_t autopatch_bench_trigger_only(void) {
    autopatch_stack_frame_t frame = {0};
    cve_target_input_t input = {0};
    uint32_t r0 = 0u;
    uint32_t r1 = 0u;
    uint32_t r2 = 0u;
    uint32_t r3 = 0u;
    volatile int ret = 0;

    cve_target_get_attack_inputs(&input);
    r0 = (uint32_t)input.queue_length;
    r1 = (uint32_t)input.item_size;
    autopatch_prepare_runtime_args_for_target(input.target, &r0, &r1, &r2, &r3);
    autopatch_prepare_frame(&frame, r0, r1, r2, r3);

    if (!cycle_counter_reset()) {
        return 0xFFFFFFFFu;
    }

    ret = autopatch_trampoline_trigger_only(AUTOPATCH_SITE_FUNC_ENTRY, &frame);
    (void)ret;
    return cycle_counter_read();
}

uint32_t autopatch_bench_dispatch_only(uint32_t active_patch_count) {
    autopatch_runtime_t backup = g_autopatch_rt;
    volatile autopatch_handler_fn_t handler = NULL;
    uint32_t cycles = 0xFFFFFFFFu;

    autopatch_prepare_dispatch_bench_table(active_patch_count);
    if (cycle_counter_reset()) {
        handler = autopatch_dispatcher_lookup((uint32_t)cve_target_get_current(), AUTOPATCH_SITE_FUNC_ENTRY);
        (void)handler;
        cycles = cycle_counter_read();
    }

    g_autopatch_rt = backup;
    return cycles;
}

uint32_t autopatch_bench_patch_exec_only(void) {
    autopatch_patch_ctx_t ctx = {0};
    const autopatch_patch_desc_t *desc = NULL;
    cve_target_input_t input = {0};
    uint32_t r0 = 0u;
    uint32_t r1 = 0u;
    uint32_t r2 = 0u;
    uint32_t r3 = 0u;
    volatile uint64_t ret = 0u;

    cve_target_get_attack_inputs(&input);
    r0 = (uint32_t)input.queue_length;
    r1 = (uint32_t)input.item_size;
    autopatch_prepare_runtime_args_for_target(input.target, &r0, &r1, &r2, &r3);
    autopatch_fill_ctx_for_target((uint32_t)input.target, AUTOPATCH_SITE_FUNC_ENTRY, r0, r1, r2, r3, &ctx);

    desc = autopatch_dispatcher_lookup_desc((uint32_t)input.target, AUTOPATCH_SITE_FUNC_ENTRY);
    if (desc == NULL || desc->handler_fn == NULL) {
        return 0xFFFFFFFFu;
    }

    if (!cycle_counter_reset()) {
        return 0xFFFFFFFFu;
    }

    ret = desc->handler_fn(&ctx);
    (void)ret;
    return cycle_counter_read();
}

void autopatch_print_status(void) {
    SEGGER_RTT_printf(0,
        "[autopatch] active=%s patch_count=%lu dispatcher backend=binary-search current_target=%s enabled_site=%s\r\n",
        autopatch_patch_is_active() ? "yes" : "no",
        (unsigned long)g_autopatch_rt.patch_count,
        cve_target_name(cve_target_get_current()),
        autopatch_site_name(AUTOPATCH_SITE_FUNC_ENTRY));
    console_puts("[autopatch] ctx model: arg0..arg3 carry trampoline-provided runtime live context.\r\n");

    for (uint32_t i = 0u; i < g_autopatch_rt.patch_count; ++i) {
        const autopatch_patch_desc_t *desc = &g_autopatch_rt.patch_table[i];

        if (!desc->active) {
            continue;
        }

        SEGGER_RTT_printf(0,
            "  patch[%lu] target=%s site=%s handler=%s@0x%08X\r\n",
            (unsigned long)i,
            cve_target_name((cve_target_t)desc->target_id),
            autopatch_site_name(desc->site_id),
            (desc->handler_name != NULL) ? desc->handler_name : "unknown",
            (uint32_t)(((uintptr_t)desc->handler_fn) & ~(uintptr_t)1u));
    }
}
