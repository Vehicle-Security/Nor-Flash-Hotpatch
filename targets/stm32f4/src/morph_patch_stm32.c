/*
 * morph_patch_stm32.c - MorphPatch for STM32F4xx (Cortex-M4).
 *
 * Drop-in replacement for src/core/patch/morph_patch.c, adapted for the
 * STM32F4 internal flash controller and ART Accelerator instruction cache.
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

#include "stm32f4xx.h"

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

/* Diagnostic: inner-cycle timings captured from the most recent
 * write_patch_halfword() call. All values are DWT cycle counts. */
volatile uint32_t g_morph_dbg_pre_unlock   = 0;
volatile uint32_t g_morph_dbg_pre_store    = 0;
volatile uint32_t g_morph_dbg_post_store   = 0;  /* after DSB/ISB/SR drain */
volatile uint32_t g_morph_dbg_post_bsy     = 0;
volatile uint32_t g_morph_dbg_bsy_iters    = 0;
volatile uint32_t g_morph_dbg_sr_first     = 0;  /* SR right after store+DSB */
volatile uint32_t g_morph_dbg_sr_final     = 0;
volatile uint32_t g_morph_dbg_total        = 0;

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
    /* STM32F4 ART Accelerator: reset instruction cache */
    __DSB();
    __ISB();
    FLASH->ACR &= ~FLASH_ACR_ICEN;    /* Disable icache */
    FLASH->ACR |= FLASH_ACR_ICRST;    /* Reset icache */
    FLASH->ACR &= ~FLASH_ACR_ICRST;
    FLASH->ACR |= FLASH_ACR_ICEN;     /* Re-enable icache */
    __DSB();
    __ISB();
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

    /* Make sure DWT is running so diagnostic timestamps are meaningful. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();

    uint32_t t_enter = DWT->CYCCNT;
    g_morph_dbg_pre_unlock = t_enter;

    /* Unlock flash */
    FLASH->KEYR = 0x45670123u;
    FLASH->KEYR = 0xCDEF89ABu;

    /* Wait for not busy */
    while (FLASH->SR & FLASH_SR_BSY) {}

    /* Set program mode, 32-bit parallelism (PSIZE=10) */
    FLASH->CR &= ~FLASH_CR_PSIZE;
    FLASH->CR |= FLASH_CR_PSIZE_1;
    FLASH->CR |= FLASH_CR_PG;

    __DSB();
    __ISB();
    uint32_t t_pre_store = DWT->CYCCNT;

    /* Write the word */
    *fw = new_val;

    /* Force the store to drain to the flash controller before polling BSY.
     * Without this, the CPU can read SR before the controller has even set
     * BSY=1, exiting the wait loop immediately and under-measuring tprog. */
    __DSB();
    __ISB();
    uint32_t sr_first = FLASH->SR;
    uint32_t t_post_store = DWT->CYCCNT;

    /* Wait for completion */
    uint32_t bsy_iters = 0u;
    uint32_t sr_final = sr_first;
    while (1) {
        sr_final = FLASH->SR;
        if ((sr_final & FLASH_SR_BSY) == 0u) {
            break;
        }
        bsy_iters++;
    }
    __DSB();
    __ISB();
    uint32_t t_post_bsy = DWT->CYCCNT;

    /* Clear PG bit and lock */
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;

    __set_PRIMASK(primask);

    g_morph_dbg_pre_store  = t_pre_store;
    g_morph_dbg_post_store = t_post_store;
    g_morph_dbg_post_bsy   = t_post_bsy;
    g_morph_dbg_bsy_iters  = bsy_iters;
    g_morph_dbg_sr_first   = sr_first;
    g_morph_dbg_sr_final   = sr_final;
    g_morph_dbg_total      = t_post_bsy - t_enter;

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
