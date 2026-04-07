/*
 * rapid.c — RapidPatch comparison implementation.
 *
 * Emulates the RapidPatch fixed-patch-point runtime.  A lightweight bytecode
 * VM executes a small filter program at each instrumented patch point.  The VM
 * supports register loads, comparisons, conditional jumps, and action opcodes
 * (PASS / DROP / REDIRECT).  A linear-scan dispatcher walks the active patch
 * list to find a matching descriptor for the current site.
 *
 * NOTE: This measures the runtime dispatch + VM execution path only.
 * Offline eBPF compilation, verification, and JIT are excluded.
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

enum {
    RAPIDPATCH_FIXED_OP_PASS = 0x00010000u,
    RAPIDPATCH_MAX_PATCHES = 64u,
    RAPIDPATCH_ACTION_REPLACE = 3u,
};

typedef enum {
    RAPID_VM_OP_LOAD_ARG = 1u,
    RAPID_VM_OP_MOV_IMM = 2u,
    RAPID_VM_OP_JEQ_IMM = 3u,
    RAPID_VM_OP_JNE_IMM = 4u,
    RAPID_VM_OP_JLT_IMM = 5u,
    RAPID_VM_OP_JGE_IMM = 6u,
    RAPID_VM_OP_UMUL32 = 7u,
    RAPID_VM_OP_SHR_IMM = 8u,
    RAPID_VM_OP_SET_ACTION = 9u,
    RAPID_VM_OP_SET_RETCODE = 10u,
    RAPID_VM_OP_RETURN = 11u,
} rapid_vm_opcode_t;

typedef enum {
    RAPID_ARG_QUEUE_LENGTH = 0,
    RAPID_ARG_ITEM_SIZE = 1,
    RAPID_ARG_AUX0 = 2,
    RAPID_ARG_AUX1 = 3,
    RAPID_ARG_AUX2 = 4,
    RAPID_ARG_AUX3 = 5,
    RAPID_ARG_TARGET_ID = 6,
} rapid_vm_arg_t;

typedef struct {
    uint8_t opcode;
    uint8_t dst;
    uint8_t src;
    int8_t offset;
    int32_t imm;
} rapid_vm_inst_t;

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t aux0;
    uint32_t aux1;
    uint32_t aux2;
    uint32_t aux3;
    uint32_t target_id;
    uint32_t lr;
} rapidpatch_fixed_frame_t;

typedef struct {
    uint16_t target_id;
    uint16_t patch_id;
    const rapid_vm_inst_t *prog;
    uint16_t prog_len;
    bool active;
} rapid_patch_desc_t;

typedef struct {
    bool active;
    uint32_t install_addr;
    uint16_t patch_count;
    rapid_patch_desc_t patch_table[RAPIDPATCH_MAX_PATCHES];
} rapidpatch_context_t;

static rapidpatch_context_t g_rapid_ctx = {0};
static volatile uint16_t g_rapid_dispatch_bench_target = 0u;

static const cve_target_profile_t g_rapid_background_profile = {
    .banner = "\r\n=== [RAPIDPATCH] Fixed Patch Point Entry ===\r\n",
    .status_line = "Status: Filter passed. The selected vulnerable body is executing.\r\n",
    .reject_prefix = "",
    .block_line = "",
    .abort_line = "",
    .done_line = "\r\n[*] RapidPatch vulnerable path finished. Returning to shell...\r\n",
    .apply_fix = false,
};

static const cve_target_profile_t g_rapid_filter_profile = {
    .banner = "\r\n=== [RAPIDPATCH] Fixed Patch Point Entry ===\r\n",
    .status_line = "Status: Fixed patch point VM rejected crafted input.\r\n",
    .reject_prefix = "RAPIDPATCH",
    .block_line = "[RAPIDPATCH] Fixed patch point blocked the crafted attack input.\r\n",
    .abort_line = "[RAPIDPATCH] Original vulnerable body was skipped.\r\n",
    .done_line = "",
    .apply_fix = false,
};

#define VM_INST(opcode, dst, src, offset, imm) \
    { (uint8_t)(opcode), (uint8_t)(dst), (uint8_t)(src), (int8_t)(offset), (int32_t)(imm) }

static const rapid_vm_inst_t g_rapid_prog_cve2024_2212[] = {
    VM_INST(RAPID_VM_OP_LOAD_ARG, 0, 0, 0, RAPID_ARG_TARGET_ID),
    VM_INST(RAPID_VM_OP_JNE_IMM, 0, 0, 16, CVE_TARGET_CVE2024_2212),
    VM_INST(RAPID_VM_OP_LOAD_ARG, 1, 0, 0, RAPID_ARG_QUEUE_LENGTH),
    VM_INST(RAPID_VM_OP_JNE_IMM, 1, 0, 3, 0),
    VM_INST(RAPID_VM_OP_SET_ACTION, 0, 0, 0, CVE_FILTER_DROP),
    VM_INST(RAPID_VM_OP_SET_RETCODE, 0, 0, 0, -1),
    VM_INST(RAPID_VM_OP_RETURN, 0, 0, 0, 0),
    VM_INST(RAPID_VM_OP_LOAD_ARG, 2, 0, 0, RAPID_ARG_ITEM_SIZE),
    VM_INST(RAPID_VM_OP_JNE_IMM, 2, 0, 3, 0),
    VM_INST(RAPID_VM_OP_SET_ACTION, 0, 0, 0, CVE_FILTER_DROP),
    VM_INST(RAPID_VM_OP_SET_RETCODE, 0, 0, 0, -2),
    VM_INST(RAPID_VM_OP_RETURN, 0, 0, 0, 0),
    VM_INST(RAPID_VM_OP_UMUL32, 1, 2, 0, 0),
    VM_INST(RAPID_VM_OP_SHR_IMM, 1, 0, 0, 32),
    VM_INST(RAPID_VM_OP_JEQ_IMM, 1, 0, 3, 0),
    VM_INST(RAPID_VM_OP_SET_ACTION, 0, 0, 0, CVE_FILTER_DROP),
    VM_INST(RAPID_VM_OP_SET_RETCODE, 0, 0, 0, PATCH_RESULT_ATTACK_BLOCKED),
    VM_INST(RAPID_VM_OP_RETURN, 0, 0, 0, 0),
    VM_INST(RAPID_VM_OP_RETURN, 0, 0, 0, 0),
};

static const rapid_vm_inst_t g_rapid_prog_cve2025_1674[] = {
    VM_INST(RAPID_VM_OP_LOAD_ARG, 0, 0, 0, RAPID_ARG_TARGET_ID),
    VM_INST(RAPID_VM_OP_JNE_IMM, 0, 0, 11, CVE_TARGET_CVE2025_1674),
    VM_INST(RAPID_VM_OP_LOAD_ARG, 1, 0, 0, RAPID_ARG_AUX0),
    VM_INST(RAPID_VM_OP_JNE_IMM, 1, 0, 9, 12),
    VM_INST(RAPID_VM_OP_LOAD_ARG, 1, 0, 0, RAPID_ARG_AUX1),
    VM_INST(RAPID_VM_OP_JNE_IMM, 1, 0, 7, 18),
    VM_INST(RAPID_VM_OP_LOAD_ARG, 1, 0, 0, RAPID_ARG_AUX2),
    VM_INST(RAPID_VM_OP_JNE_IMM, 1, 0, 5, 3),
    VM_INST(RAPID_VM_OP_LOAD_ARG, 1, 0, 0, RAPID_ARG_AUX3),
    VM_INST(RAPID_VM_OP_JGE_IMM, 1, 0, 3, 10),
    VM_INST(RAPID_VM_OP_SET_ACTION, 0, 0, 0, CVE_FILTER_DROP),
    VM_INST(RAPID_VM_OP_SET_RETCODE, 0, 0, 0, PATCH_RESULT_REJECT_DNS_BOUNDS),
    VM_INST(RAPID_VM_OP_RETURN, 0, 0, 0, 0),
    VM_INST(RAPID_VM_OP_RETURN, 0, 0, 0, 0),
};

static const rapid_vm_inst_t g_rapid_prog_cve2025_12899[] = {
    VM_INST(RAPID_VM_OP_LOAD_ARG, 0, 0, 0, RAPID_ARG_TARGET_ID),
    VM_INST(RAPID_VM_OP_JNE_IMM, 0, 0, 11, CVE_TARGET_CVE2025_12899),
    VM_INST(RAPID_VM_OP_LOAD_ARG, 1, 0, 0, RAPID_ARG_AUX0),
    VM_INST(RAPID_VM_OP_JNE_IMM, 1, 0, 9, 4),
    VM_INST(RAPID_VM_OP_LOAD_ARG, 1, 0, 0, RAPID_ARG_AUX1),
    VM_INST(RAPID_VM_OP_JNE_IMM, 1, 0, 7, 6),
    VM_INST(RAPID_VM_OP_LOAD_ARG, 1, 0, 0, RAPID_ARG_AUX2),
    VM_INST(RAPID_VM_OP_JNE_IMM, 1, 0, 5, 128),
    VM_INST(RAPID_VM_OP_LOAD_ARG, 1, 0, 0, RAPID_ARG_AUX3),
    VM_INST(RAPID_VM_OP_JNE_IMM, 1, 0, 3, 0),
    VM_INST(RAPID_VM_OP_SET_ACTION, 0, 0, 0, CVE_FILTER_DROP),
    VM_INST(RAPID_VM_OP_SET_RETCODE, 0, 0, 0, PATCH_RESULT_REJECT_ICMP_FAMILY),
    VM_INST(RAPID_VM_OP_RETURN, 0, 0, 0, 0),
    VM_INST(RAPID_VM_OP_RETURN, 0, 0, 0, 0),
};

static const rapid_patch_desc_t g_rapid_builtin_descs[] = {
    {
        .target_id = (uint16_t)CVE_TARGET_CVE2024_2212,
        .patch_id = 1u,
        .prog = g_rapid_prog_cve2024_2212,
        .prog_len = (uint16_t)(sizeof(g_rapid_prog_cve2024_2212) / sizeof(g_rapid_prog_cve2024_2212[0])),
        .active = true,
    },
    {
        .target_id = (uint16_t)CVE_TARGET_CVE2025_1674,
        .patch_id = 2u,
        .prog = g_rapid_prog_cve2025_1674,
        .prog_len = (uint16_t)(sizeof(g_rapid_prog_cve2025_1674) / sizeof(g_rapid_prog_cve2025_1674[0])),
        .active = true,
    },
    {
        .target_id = (uint16_t)CVE_TARGET_CVE2025_12899,
        .patch_id = 3u,
        .prog = g_rapid_prog_cve2025_12899,
        .prog_len = (uint16_t)(sizeof(g_rapid_prog_cve2025_12899) / sizeof(g_rapid_prog_cve2025_12899[0])),
        .active = true,
    },
};

static uint32_t rapid_patch_install_addr(void) {
    return (uint32_t)(((uintptr_t)rapid_invoke) & ~(uintptr_t)1u);
}

static uint64_t rapid_encode_vm_result(uint32_t op, int32_t ret_code) {
    return ((uint64_t)op << 32) | (uint32_t)ret_code;
}

static uint32_t rapid_frame_load_arg(const rapidpatch_fixed_frame_t *frame, int32_t arg_id) {
    if (frame == NULL) {
        return 0u;
    }

    if (arg_id == RAPID_ARG_QUEUE_LENGTH) {
        return frame->r0;
    }
    if (arg_id == RAPID_ARG_ITEM_SIZE) {
        return frame->r1;
    }
    if (arg_id == RAPID_ARG_AUX0) {
        return frame->aux0;
    }
    if (arg_id == RAPID_ARG_AUX1) {
        return frame->aux1;
    }
    if (arg_id == RAPID_ARG_AUX2) {
        return frame->aux2;
    }
    if (arg_id == RAPID_ARG_AUX3) {
        return frame->aux3;
    }
    if (arg_id == RAPID_ARG_TARGET_ID) {
        return frame->target_id;
    }
    return 0u;
}

static bool rapid_vm_branch(size_t *pc, int8_t offset, size_t inst_count) {
    int32_t target = 0;

    if (pc == NULL) {
        return false;
    }

    target = (int32_t)(*pc) + (int32_t)offset;
    if (target < 0 || (size_t)target >= inst_count) {
        return false;
    }

    *pc = (size_t)target;
    return true;
}

static uint64_t rapid_vm_exec(const rapid_vm_inst_t *prog,
                              uint16_t prog_len,
                              const rapidpatch_fixed_frame_t *frame) {
    uint64_t regs[8] = {0};
    uint32_t action = CVE_FILTER_PASS;
    int32_t ret_code = PATCH_RESULT_SAFE_NOOP;
    size_t pc = 0u;

    if (prog == NULL || prog_len == 0u || frame == NULL) {
        return UINT64_MAX;
    }

    while (pc < prog_len) {
        const rapid_vm_inst_t *inst = &prog[pc++];
        uint32_t imm_u32 = (uint32_t)inst->imm;

        if (inst->dst >= (uint8_t)(sizeof(regs) / sizeof(regs[0])) ||
            inst->src >= (uint8_t)(sizeof(regs) / sizeof(regs[0]))) {
            return UINT64_MAX;
        }

        switch ((rapid_vm_opcode_t)inst->opcode) {
        case RAPID_VM_OP_LOAD_ARG:
            regs[inst->dst] = rapid_frame_load_arg(frame, inst->imm);
            break;

        case RAPID_VM_OP_MOV_IMM:
            regs[inst->dst] = imm_u32;
            break;

        case RAPID_VM_OP_JEQ_IMM:
            if ((uint32_t)regs[inst->dst] == imm_u32 &&
                !rapid_vm_branch(&pc, inst->offset, prog_len)) {
                return UINT64_MAX;
            }
            break;

        case RAPID_VM_OP_JNE_IMM:
            if ((uint32_t)regs[inst->dst] != imm_u32 &&
                !rapid_vm_branch(&pc, inst->offset, prog_len)) {
                return UINT64_MAX;
            }
            break;

        case RAPID_VM_OP_JLT_IMM:
            if ((uint32_t)regs[inst->dst] < imm_u32 &&
                !rapid_vm_branch(&pc, inst->offset, prog_len)) {
                return UINT64_MAX;
            }
            break;

        case RAPID_VM_OP_JGE_IMM:
            if ((uint32_t)regs[inst->dst] >= imm_u32 &&
                !rapid_vm_branch(&pc, inst->offset, prog_len)) {
                return UINT64_MAX;
            }
            break;

        case RAPID_VM_OP_UMUL32:
            regs[inst->dst] = (uint64_t)(uint32_t)regs[inst->dst] * (uint64_t)(uint32_t)regs[inst->src];
            break;

        case RAPID_VM_OP_SHR_IMM:
            regs[inst->dst] >>= (uint32_t)inst->imm;
            break;

        case RAPID_VM_OP_SET_ACTION:
            action = imm_u32;
            break;

        case RAPID_VM_OP_SET_RETCODE:
            ret_code = inst->imm;
            break;

        case RAPID_VM_OP_RETURN:
            return rapid_encode_vm_result(action, ret_code);

        default:
            return UINT64_MAX;
        }
    }

    return UINT64_MAX;
}

static void rapid_prepare_frame(rapidpatch_fixed_frame_t *frame,
                                uint32_t r0,
                                uint32_t r1,
                                uint32_t r2,
                                uint32_t r3) {
    cve_target_t target = cve_target_get_current();

    if (frame == NULL) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->target_id = (uint32_t)target;
    frame->lr = g_rapid_ctx.install_addr;

    if (target == CVE_TARGET_CVE2024_2212) {
        frame->r0 = r0;
        frame->r1 = r1;
        return;
    }

    frame->r0 = r0;
    frame->r1 = r1;
    frame->r2 = r2;
    frame->r3 = r3;
    frame->aux0 = r0;
    frame->aux1 = r1;
    frame->aux2 = r2;
    frame->aux3 = r3;
}

static cve_target_input_t rapid_prepare_input(cve_target_t target,
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
    } else {
        /*
         * Compare/demo simplification:
         * map invoke runtime args into target input slots so PASS/Continue path
         * can consume the same live args that VM used for PASS/DROP decisions.
         */
        input.runtime_arg0 = r0;
        input.runtime_arg1 = r1;
        input.runtime_arg2 = r2;
        input.runtime_arg3 = r3;
    }

    return input;
}

static const rapid_patch_desc_t *rapid_dispatcher_lookup(uint16_t target_id) {
    for (uint16_t i = 0; i < g_rapid_ctx.patch_count; ++i) {
        if (!g_rapid_ctx.patch_table[i].active) {
            continue;
        }
        if (g_rapid_ctx.patch_table[i].target_id == target_id) {
            return &g_rapid_ctx.patch_table[i];
        }
    }

    return NULL;
}

static __attribute__((noinline)) const rapid_patch_desc_t *rapid_dispatcher_lookup_only(uint16_t target_id) {
    uint16_t patch_count = g_rapid_ctx.patch_count;
    const volatile rapid_patch_desc_t *table = g_rapid_ctx.patch_table;

    for (uint16_t i = 0; i < patch_count; ++i) {
        if (!table[i].active) {
            continue;
        }
        if (table[i].target_id == target_id) {
            return &g_rapid_ctx.patch_table[i];
        }
    }

    return NULL;
}

static uint64_t rapid_dispatcher_entry(const rapidpatch_fixed_frame_t *frame,
                                       bool do_lookup,
                                       bool do_execute_body) {
    const rapid_patch_desc_t *desc = NULL;

    if (!do_lookup) {
        return rapid_encode_vm_result(CVE_FILTER_PASS, PATCH_RESULT_SAFE_NOOP);
    }

    desc = rapid_dispatcher_lookup((uint16_t)frame->target_id);
    if (desc == NULL || !do_execute_body) {
        return rapid_encode_vm_result(CVE_FILTER_PASS, PATCH_RESULT_SAFE_NOOP);
    }

    return rapid_vm_exec(desc->prog, desc->prog_len, frame);
}

static int rapid_decode_vm_ret(uint64_t vm_ret) {
    uint32_t op = 0u;
    int32_t ret_code = -127;

    if (vm_ret == UINT64_MAX) {
        return -127;
    }

    op = (uint32_t)(vm_ret >> 32);
    ret_code = (int32_t)(uint32_t)vm_ret;

    if (op == CVE_FILTER_PASS) {
        return (int)RAPIDPATCH_FIXED_OP_PASS;
    }

    if (op == CVE_FILTER_DROP || op == CVE_FILTER_REDIRECT || op == RAPIDPATCH_ACTION_REPLACE) {
        return ret_code;
    }

    return -127;
}

static int rapid_fixed_patch_point_trigger_only(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    rapidpatch_fixed_frame_t frame = {0};

    if (!g_rapid_ctx.active) {
        return (int)RAPIDPATCH_FIXED_OP_PASS;
    }

    rapid_prepare_frame(&frame, r0, r1, r2, r3);
    (void)rapid_dispatcher_entry(&frame, false, false);
    return (int)RAPIDPATCH_FIXED_OP_PASS;
}

int rapid_fixed_patch_point_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    rapidpatch_fixed_frame_t frame = {0};
    uint64_t vm_ret = 0u;

    if (!g_rapid_ctx.active) {
        return (int)RAPIDPATCH_FIXED_OP_PASS;
    }

    rapid_prepare_frame(&frame, r0, r1, r2, r3);
    vm_ret = rapid_dispatcher_entry(&frame, true, true);
    return rapid_decode_vm_ret(vm_ret);
}

static bool rapid_patch_install_impl(void) {
    size_t patch_count = sizeof(g_rapid_builtin_descs) / sizeof(g_rapid_builtin_descs[0]);

    if (patch_count > RAPIDPATCH_MAX_PATCHES) {
        return false;
    }

    memset(&g_rapid_ctx, 0, sizeof(g_rapid_ctx));
    memcpy(g_rapid_ctx.patch_table, g_rapid_builtin_descs, sizeof(g_rapid_builtin_descs));
    g_rapid_ctx.patch_count = (uint16_t)patch_count;
    g_rapid_ctx.install_addr = rapid_patch_install_addr();
    g_rapid_ctx.active = true;

    __DSB();
    __ISB();
    return true;
}

bool rapid_patch_install(void) {
    /*
     * Runtime-only compare mode:
     * this install path only enables fixed patch-point runtime dispatch + VM patches.
     * verifier/JIT/offline patch generation are intentionally excluded from timing.
     */
    return rapid_patch_install_impl();
}

void rapid_patch_unapply(void) {
    memset(&g_rapid_ctx, 0, sizeof(g_rapid_ctx));
    __DSB();
    __ISB();
}

bool rapid_patch_is_active(void) {
    return g_rapid_ctx.active;
}

int rapid_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    cve_target_t target = cve_target_get_current();
    cve_target_input_t input = rapid_prepare_input(target, r0, r1, r2, r3);
    bool verbose = app_exec_mode_is_verbose();
    int patch_ret = rapid_fixed_patch_point_invoke(r0, r1, r2, r3);

    if (patch_ret != (int)RAPIDPATCH_FIXED_OP_PASS) {
        return patch_ret;
    }

    return cve_target_run(&input, verbose, &g_rapid_background_profile);
}

int rapid_patch_slot(void) {
    cve_target_input_t input = {0};
    int ret = 0;
    bool verbose = app_exec_mode_is_verbose();
    bool auto_fed = false;
    uint32_t r0 = 0u;
    uint32_t r1 = 0u;
    uint32_t r2 = 0u;
    uint32_t r3 = 0u;

    if (verbose) {
        console_puts("\r\n=== [RAPIDPATCH] Fixed Patch Point Entry ===\r\n");
    }

    if (!cve_target_fetch_inputs(&input, &auto_fed)) {
        console_puts("[-] Failed to collect input for the selected vulnerability.\r\n");
        return -127;
    }

    if (verbose && auto_fed) {
        console_puts("[DEMO] Loaded attack input for the selected vulnerability.\r\n");
    }

    if (input.target == CVE_TARGET_CVE2024_2212) {
        r0 = (uint32_t)input.queue_length;
        r1 = (uint32_t)input.item_size;
    } else {
        r0 = input.runtime_arg0;
        r1 = input.runtime_arg1;
        r2 = input.runtime_arg2;
        r3 = input.runtime_arg3;
    }

    ret = rapid_invoke(r0, r1, r2, r3);
    if (verbose &&
        patch_result_is_fixed(ret) &&
        ret != PATCH_RESULT_SAFE_EXECUTED &&
        ret != PATCH_RESULT_SAFE_NOOP) {
        SEGGER_RTT_printf(0,
            "\r\n[RAPIDPATCH] VM rejected %s at the fixed patch point, ret=%d\r\n",
            cve_target_name(input.target),
            ret);
        console_puts(g_rapid_filter_profile.block_line);
        console_puts(g_rapid_filter_profile.abort_line);
        return ret;
    }

    if (verbose && ret != 0) {
        SEGGER_RTT_printf(0, "\r\n[RAPIDPATCH] Target returned %d\r\n", ret);
    }

    return ret;
}

static uint32_t rapid_measure_vm_exec(const rapid_patch_desc_t *desc,
                                      const rapidpatch_fixed_frame_t *frame) {
    volatile uint64_t vm_ret = 0u;

    if (desc == NULL || frame == NULL) {
        return 0xFFFFFFFFu;
    }

    if (!cycle_counter_reset()) {
        return 0xFFFFFFFFu;
    }

    vm_ret = rapid_vm_exec(desc->prog, desc->prog_len, frame);
    (void)vm_ret;
    return cycle_counter_read();
}

uint32_t rapid_bench_trigger_only(void) {
    cve_target_input_t input = {0};
    uint32_t r0 = 0u;
    uint32_t r1 = 0u;
    uint32_t r2 = 0u;
    uint32_t r3 = 0u;
    volatile int ret = 0;

    cve_target_get_attack_inputs(&input);
    if (input.target == CVE_TARGET_CVE2024_2212) {
        r0 = (uint32_t)input.queue_length;
        r1 = (uint32_t)input.item_size;
    } else {
        r0 = input.runtime_arg0;
        r1 = input.runtime_arg1;
        r2 = input.runtime_arg2;
        r3 = input.runtime_arg3;
    }

    if (!cycle_counter_reset()) {
        return 0xFFFFFFFFu;
    }

    ret = rapid_fixed_patch_point_trigger_only(r0, r1, r2, r3);
    (void)ret;
    return cycle_counter_read();
}

static void rapid_prepare_dispatch_bench_table(uint32_t active_patch_count, uint16_t target_id) {
    uint32_t count = active_patch_count;

    if (count == 0u) {
        count = 1u;
    }
    if (count > RAPIDPATCH_MAX_PATCHES) {
        count = RAPIDPATCH_MAX_PATCHES;
    }

    for (uint32_t i = 0; i < count; ++i) {
        g_rapid_ctx.patch_table[i].target_id = (uint16_t)(1000u + i);
        g_rapid_ctx.patch_table[i].patch_id = (uint16_t)(i + 1u);
        g_rapid_ctx.patch_table[i].prog = g_rapid_prog_cve2024_2212;
        g_rapid_ctx.patch_table[i].prog_len =
            (uint16_t)(sizeof(g_rapid_prog_cve2024_2212) / sizeof(g_rapid_prog_cve2024_2212[0]));
        g_rapid_ctx.patch_table[i].active = true;
    }

    /* Force near-tail hit so linear-scan cost scales with active patch count. */
    g_rapid_ctx.patch_table[count - 1u].target_id = target_id;
    g_rapid_dispatch_bench_target = target_id;
    g_rapid_ctx.patch_count = (uint16_t)count;
}

uint32_t rapid_bench_dispatch_only(uint32_t active_patch_count) {
    rapid_patch_desc_t backup_table[RAPIDPATCH_MAX_PATCHES];
    uint16_t backup_count = g_rapid_ctx.patch_count;
    bool backup_active = g_rapid_ctx.active;
    const rapid_patch_desc_t *desc = NULL;
    volatile uint32_t sink = 0u;
    uint32_t cycles = 0xFFFFFFFFu;

    memcpy(backup_table, g_rapid_ctx.patch_table, sizeof(backup_table));

    rapid_prepare_dispatch_bench_table(active_patch_count, (uint16_t)cve_target_get_current());
    g_rapid_ctx.active = true;

    if (cycle_counter_reset()) {
        /*
         * Lookup-only benchmark path:
         * measure dispatcher table scan cost without VM execution.
         */
        desc = rapid_dispatcher_lookup_only(g_rapid_dispatch_bench_target);
        sink = (desc != NULL) ? desc->patch_id : 0u;
        (void)sink;
        cycles = cycle_counter_read();
    }

    memcpy(g_rapid_ctx.patch_table, backup_table, sizeof(backup_table));
    g_rapid_ctx.patch_count = backup_count;
    g_rapid_ctx.active = backup_active;
    return cycles;
}

uint32_t rapid_bench_patch_exec_only(void) {
    rapidpatch_fixed_frame_t frame = {0};
    cve_target_input_t input = {0};
    uint32_t r0 = 0u;
    uint32_t r1 = 0u;
    uint32_t r2 = 0u;
    uint32_t r3 = 0u;
    const rapid_patch_desc_t *desc = NULL;

    cve_target_get_attack_inputs(&input);
    if (input.target == CVE_TARGET_CVE2024_2212) {
        r0 = (uint32_t)input.queue_length;
        r1 = (uint32_t)input.item_size;
    } else {
        r0 = input.runtime_arg0;
        r1 = input.runtime_arg1;
        r2 = input.runtime_arg2;
        r3 = input.runtime_arg3;
    }

    rapid_prepare_frame(&frame, r0, r1, r2, r3);
    desc = rapid_dispatcher_lookup((uint16_t)frame.target_id);
    return rapid_measure_vm_exec(desc, &frame);
}

void rapid_patch_print_status(void) {
    SEGGER_RTT_printf(0,
        "[rapid] active=%s patch_count=%u dispatcher=linear-scan vm=bytecode target=%s install_addr=0x%08X\r\n",
        g_rapid_ctx.active ? "yes" : "no",
        g_rapid_ctx.patch_count,
        cve_target_name(cve_target_get_current()),
        g_rapid_ctx.install_addr);
}

memory_cost_t rapid_memory_cost(void) {
    memory_cost_t cost;

    cost.flash_bytes = (uint32_t)(sizeof(g_rapid_prog_cve2024_2212)
                                + sizeof(g_rapid_prog_cve2025_1674)
                                + sizeof(g_rapid_prog_cve2025_12899)
                                + sizeof(g_rapid_builtin_descs));
    cost.ram_bytes   = (uint32_t)(sizeof(g_rapid_ctx));
    return cost;
}
