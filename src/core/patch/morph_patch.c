/*
 * morph_patch.c - MorphPatch: NOR-flash hotpatching via monotonic bit-clear.
 *
 * NOR flash allows only 1->0 bit transitions without a sector erase.
 * MorphPatch now supports two monotonic dispatch paths from the same factory
 * image:
 *
 * 1. direct-branch path: 0xE7FF -> Thumb B(fun2) -> 0xE000
 * 2. fault-dispatch path: 0xE7FF -> 0x4700 -> 0x4600
 *
 * The direct-branch path is the original implementation and remains the
 * default. The new fault-dispatch path uses "bx r0" to raise a controlled
 * fault, and the fault handler then redirects execution to the fix function.
 *
 * The fault-path entry preserves the caller's original R0 in R12 before
 * forcing R0 to a faulting value, and the fault handler restores stacked R0
 * from stacked R12 before redirecting directly to fun2.
 */
#include "morph_patch.h"

#include <stdint.h>

#include "nrf.h"

#include "../console/console.h"
#include "../target/targets.h"

extern uint32_t __hotpatch_page_start__;
extern uint32_t __hotpatch_page_end__;

#define MORPH_PATCH_ORIGINAL_HALFWORD       0xE7FFu  /* Thumb: b.n +2 */
#define MORPH_PATCH_DIRECT_UNPATCH_HALFWORD 0xE000u  /* Thumb: b.n +0 */
#define MORPH_PATCH_FAULT_ACTIVE_HALFWORD   0x4700u  /* Thumb: bx r0 */
#define MORPH_PATCH_FAULT_UNPATCH_HALFWORD  0x4600u  /* Thumb: mov r0, r0 */

/* Fault-dispatch context read by hardfault_handler.c. */
volatile uintptr_t g_patch_dispatch_target = 0;
volatile bool      g_patch_dispatch_pending = false;

static volatile morph_patch_path_t g_morph_patch_path = MORPH_PATCH_PATH_DIRECT;

static const char *morph_patch_path_name_internal(morph_patch_path_t path) {
    return (path == MORPH_PATCH_PATH_FAULT) ? "fault-dispatch" : "direct-branch";
}

void morph_patch_set_path(morph_patch_path_t path) {
    g_morph_patch_path = (path == MORPH_PATCH_PATH_FAULT)
        ? MORPH_PATCH_PATH_FAULT
        : MORPH_PATCH_PATH_DIRECT;
}

morph_patch_path_t morph_patch_get_path(void) {
    return g_morph_patch_path;
}

const char *morph_patch_path_name(void) {
    return morph_patch_path_name_internal(g_morph_patch_path);
}

static uintptr_t patch_slot_addr(void) {
    return ((uintptr_t)patch_slot) & ~(uintptr_t)1u;
}

static void invalidate_code_cache(void) {
#if defined(NVMC_FEATURE_CACHE_PRESENT)
    uint32_t icache = NRF_NVMC->ICACHECNF;

    NRF_NVMC->ICACHECNF =
        (icache & ~NVMC_ICACHECNF_CACHEEN_Msk) |
        (NVMC_ICACHECNF_CACHEEN_Disabled << NVMC_ICACHECNF_CACHEEN_Pos);

    __DSB();
    __ISB();

    NRF_NVMC->ICACHECNF = icache;

    __DSB();
    __ISB();
#endif
}

static bool build_direct_branch_instr(uint16_t *out_instr, bool verbose) {
    uintptr_t from = patch_slot_addr();
    uintptr_t to = ((uintptr_t)fun2) & ~(uintptr_t)1u;
    int32_t diff = (int32_t)to - (int32_t)(from + 4u);

    if ((diff & 1) != 0) {
        if (verbose) {
            console_puts("[-] patch target is not halfword aligned.\r\n");
        }
        return false;
    }

    if (diff < -2048 || diff > 2046) {
        if (verbose) {
            console_printf(
                "[-] fun2 entry out of 16-bit Thumb B range. diff=%d bytes\r\n",
                diff);
        }
        return false;
    }

    *out_instr = (uint16_t)(0xE000u | (((uint32_t)(diff >> 1)) & 0x07FFu));
    return true;
}

static bool build_selected_apply_instr(uint16_t *out_instr, bool verbose) {
    if (g_morph_patch_path == MORPH_PATCH_PATH_FAULT) {
        *out_instr = MORPH_PATCH_FAULT_ACTIVE_HALFWORD;
        return true;
    }

    return build_direct_branch_instr(out_instr, verbose);
}

static void write_patch_halfword(uint16_t instr) {
    uintptr_t patch_addr = patch_slot_addr();
    uint32_t aligned_addr = (uint32_t)(patch_addr & ~(uintptr_t)3u);
    volatile uint32_t *fw = (volatile uint32_t *)aligned_addr;

    uint32_t old_val = *fw;
    uint32_t new_val = 0u;

    if ((patch_addr & 2u) == 0u) {
        new_val = (old_val & 0xFFFF0000u) | instr;
    } else {
        new_val = (old_val & 0x0000FFFFu) | ((uint32_t)instr << 16);
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    NRF_NVMC->CONFIG = 1;
    while (NRF_NVMC->READY == 0) {
    }

    *fw = new_val;
    while (NRF_NVMC->READY == 0) {
    }

    NRF_NVMC->CONFIG = 0;
    while (NRF_NVMC->READY == 0) {
    }

    __set_PRIMASK(primask);

    __DSB();
    __ISB();
    invalidate_code_cache();
}

static uint16_t read_patch_halfword(void) {
    uintptr_t patch_addr = patch_slot_addr();
    uint32_t aligned_addr = (uint32_t)(patch_addr & ~(uintptr_t)3u);
    uint32_t value = *(volatile uint32_t *)aligned_addr;

    if ((patch_addr & 2u) == 0u) {
        return (uint16_t)(value & 0xFFFFu);
    }
    return (uint16_t)(value >> 16);
}

static void morph_patch_dispatch_arm(int (*target_fn)(void)) {
    g_patch_dispatch_target = ((uintptr_t)target_fn) | 1u;
    g_patch_dispatch_pending = true;
    __DSB();
    __ISB();
}

static void morph_patch_dispatch_disarm(void) {
    g_patch_dispatch_pending = false;
    g_patch_dispatch_target = 0;
    __DSB();
    __ISB();
}

/*
 * patch_slot starts with one mutable halfword followed by the original
 * fall-through body:
 *   [0] mutable halfword (factory 0xE7FF / direct-branch / fault-trigger / unpatch)
 *   [1] nop
 *   [2] b   fun1
 *
 * Only the fault wrapper preserves R0 in R12 and then clears R0 so
 * 0x4700 ("bx r0") faults in a deterministic way.
 */
__attribute__((naked, noinline)) int morph_patch_fault_entry(void) {
    __asm volatile(
        ".thumb          \n"
        "mov   ip, r0    \n"
        "movs  r0, #0    \n"
        "b.w   patch_slot\n");
    __builtin_unreachable();
}

void morph_patch_fault_begin(void) {
    morph_patch_dispatch_arm(fun2);
}

void morph_patch_fault_end(void) {
    morph_patch_dispatch_disarm();
}

static uintptr_t morph_patch_dispatch_fault_recover(uint32_t *stacked_frame) {
    uintptr_t redirect = 0;

    if (!g_patch_dispatch_pending) {
        return 0;
    }

    redirect = g_patch_dispatch_target;
    morph_patch_dispatch_disarm();

    if (read_patch_halfword() != MORPH_PATCH_FAULT_ACTIVE_HALFWORD) {
        return 0;
    }

    /* Wrapper saved the caller's original R0 in R12 before forcing the fault. */
    stacked_frame[0] = stacked_frame[4];

    return redirect;
}

uintptr_t morph_patch_fault_recover(uint32_t *stacked_frame) {
    return morph_patch_dispatch_fault_recover(stacked_frame);
}

static bool morph_patch_halfword_can_reach(uint16_t current, uint16_t target) {
    return (current & target) == target;
}

static bool morph_patch_program_halfword_retry(uint16_t target_instr,
                                               const char *op_name) {
    uint16_t current = read_patch_halfword();
    uint16_t readback = 0;

    if (!morph_patch_halfword_can_reach(current, target_instr)) {
        console_printf(
            "[-] Cannot %s without erase. current=0x%04X target=0x%04X\r\n",
            op_name,
            current,
            target_instr);
        return false;
    }

    write_patch_halfword(target_instr);

    readback = read_patch_halfword();
    if (readback == target_instr) {
        return true;
    }

    console_printf(
        "[!] %s verify failed: wrote 0x%04X, read 0x%04X\r\n",
        op_name,
        target_instr,
        readback);

    if (!morph_patch_halfword_can_reach(readback, target_instr)) {
        console_printf("[!] %s cannot retry without erase.\r\n", op_name);
        return false;
    }

    console_printf("[!] %s retrying write (1->0 still possible)...\r\n", op_name);
    write_patch_halfword(target_instr);
    readback = read_patch_halfword();
    if (readback == target_instr) {
        console_printf("[+] %s retry succeeded.\r\n", op_name);
        return true;
    }

    console_printf("[!] %s retry failed.\r\n", op_name);
    return false;
}

static uint16_t selected_unapply_halfword(void) {
    uint16_t current = read_patch_halfword();
    uint16_t direct_branch_instr = 0;

    if (current == MORPH_PATCH_FAULT_ACTIVE_HALFWORD
        || current == MORPH_PATCH_FAULT_UNPATCH_HALFWORD) {
        return MORPH_PATCH_FAULT_UNPATCH_HALFWORD;
    }

    if (build_direct_branch_instr(&direct_branch_instr, false)
        && current == direct_branch_instr) {
        return MORPH_PATCH_DIRECT_UNPATCH_HALFWORD;
    }

    return (g_morph_patch_path == MORPH_PATCH_PATH_FAULT)
        ? MORPH_PATCH_FAULT_UNPATCH_HALFWORD
        : MORPH_PATCH_DIRECT_UNPATCH_HALFWORD;
}

bool morph_patch_apply(void) {
    uint16_t target_instr = 0;

    if (build_selected_apply_instr(&target_instr, true)
        && morph_patch_program_halfword_retry(target_instr, "patch apply")) {
        return true;
    }

    if (g_morph_patch_path != MORPH_PATCH_PATH_DIRECT) {
        return false;
    }

    console_puts("[!] direct-branch apply failed. Falling back to fault-dispatch.\r\n");
    if (!morph_patch_program_halfword_retry(
            MORPH_PATCH_FAULT_ACTIVE_HALFWORD,
            "patch apply fallback")) {
        console_puts("[-] fault-dispatch fallback install failed.\r\n");
        return false;
    }

    console_puts("[+] fault-dispatch fallback installed.\r\n");
    return true;
}

void morph_patch_unapply(void) {
    (void)morph_patch_program_halfword_retry(
        selected_unapply_halfword(),
        "patch unapply");
}

bool morph_patch_is_active(void) {
    uint16_t instr = read_patch_halfword();
    uint16_t direct_branch_instr = 0;

    if (instr == MORPH_PATCH_FAULT_ACTIVE_HALFWORD) {
        return instr == MORPH_PATCH_FAULT_ACTIVE_HALFWORD;
    }

    if (!build_direct_branch_instr(&direct_branch_instr, false)) {
        return false;
    }

    return instr == direct_branch_instr;
}

bool morph_patch_current_slot_uses_fault_dispatch(void) {
    return read_patch_halfword() == MORPH_PATCH_FAULT_ACTIVE_HALFWORD;
}

bool morph_patch_demo_can_run(void) {
    return read_patch_halfword() == MORPH_PATCH_ORIGINAL_HALFWORD;
}

void morph_patch_print_status(void) {
    uint16_t instr = read_patch_halfword();
    uint16_t direct_branch_instr = 0;
    bool has_direct_branch = build_direct_branch_instr(&direct_branch_instr, false);

    console_printf("[MorphPatch] selected path: %s\r\n", morph_patch_path_name());
    console_printf("[MorphPatch] patch_slot first halfword: 0x%04X\r\n", instr);

    if (has_direct_branch && instr == direct_branch_instr) {
        console_puts("[MorphPatch] slot state: direct-branch to fun2\r\n");
    } else if (instr == MORPH_PATCH_FAULT_ACTIVE_HALFWORD) {
        if (g_morph_patch_path == MORPH_PATCH_PATH_DIRECT) {
            console_puts("[MorphPatch] slot state: fault-dispatch backup to fun2\r\n");
        } else {
            console_puts("[MorphPatch] slot state: fault-dispatch to fun2\r\n");
        }
    } else if (instr == MORPH_PATCH_ORIGINAL_HALFWORD) {
        console_puts("[MorphPatch] slot state: factory fall-through (fun1 path)\r\n");
    } else if (instr == MORPH_PATCH_DIRECT_UNPATCH_HALFWORD) {
        console_puts("[MorphPatch] slot state: direct-path unpatched (fun1 path)\r\n");
    } else if (instr == MORPH_PATCH_FAULT_UNPATCH_HALFWORD) {
        if (g_morph_patch_path == MORPH_PATCH_PATH_DIRECT) {
            console_puts("[MorphPatch] slot state: fault-backup unpatched (fun1 path)\r\n");
        } else {
            console_puts("[MorphPatch] slot state: fault-path unpatched (fun1 path)\r\n");
        }
    } else {
        console_puts("[MorphPatch] slot state: unknown\r\n");
    }
}

__attribute__((naked, noinline, used, section(".hotpatch_page.slot"), aligned(2)))
int patch_slot(void) {
    __asm volatile(
        ".thumb        \n"
        ".hword 0xE7FF \n"
        "nop           \n"
        "b     fun1    \n");
}

memory_cost_t morph_patch_memory_cost(void) {
    memory_cost_t cost;

    cost.flash_bytes = (uint32_t)((uintptr_t)&__hotpatch_page_end__
                                - (uintptr_t)&__hotpatch_page_start__);
    cost.ram_bytes = (uint32_t)(sizeof(g_patch_dispatch_target)
                              + sizeof(g_patch_dispatch_pending)
                              + sizeof(g_morph_patch_path));
    return cost;
}
