/*
 * clearbit_patch.c — ClearBitPatch: NOR-flash hotpatching via monotonic bit-clear.
 *
 * NOR flash allows only 1->0 bit transitions without a sector erase.
 * ClearBitPatch exploits this by pre-filling a patch slot with 0xE7FF
 * (Thumb infinite-loop) and later clearing selected bits to form a
 * 16-bit Thumb B (branch) instruction that redirects to the fix function.
 *
 * Write-verify-retry logic ensures the flash word was programmed correctly.
 * If verification fails after retry, HardFault is triggered so the
 * exception handler can attempt recovery or redirect execution to the
 * fix function as a last resort.
 */
#include "clearbit_patch.h"

#include <stdint.h>

#include "nrf.h"

#include "../console/console.h"
#include "../target/targets.h"

extern uint32_t __hotpatch_page_start__;
extern uint32_t __hotpatch_page_end__;

#define CLEARBIT_PATCH_ORIGINAL_HALFWORD 0xE7FFu  /* Thumb: b . (infinite loop) */
#define CLEARBIT_PATCH_UNPATCH_HALFWORD  0xE000u  /* Thumb: b +0 (fall through) */

/* ---- HardFault recovery context (read by hardfault_handler.c) ---- */
volatile uintptr_t g_patch_recovery_target = 0;
volatile uint16_t  g_patch_recovery_instr  = 0;
volatile bool      g_patch_recovery_pending = false;

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

static bool build_clearbit_patch_branch_instr(uint16_t *out_instr, bool verbose) {
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

    /* Encode 16-bit Thumb B: opcode 0xE000 | signed 11-bit offset */
    *out_instr = (uint16_t)(0xE000u | (((uint32_t)(diff >> 1)) & 0x07FFu));
    return true;
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

static void clearbit_hardfault_recovery_trigger(uint16_t instr,
                                                int (*target_fn)(void)) {
    g_patch_recovery_target = ((uintptr_t)target_fn) | 1u;
    g_patch_recovery_instr  = instr;
    g_patch_recovery_pending = true;
    __DSB();
    __asm volatile("udf #0");
}

uintptr_t clearbit_patch_hardfault_recover(void) {
    if (!g_patch_recovery_pending) {
        return 0;
    }
    g_patch_recovery_pending = false;

    write_patch_halfword(g_patch_recovery_instr);

    uint16_t readback = read_patch_halfword();
    if (readback == g_patch_recovery_instr) {
        return 0;
    }
    return g_patch_recovery_target;
}

bool clearbit_patch_apply(void) {
    uint16_t branch_instr = 0;
    uint16_t current = 0;
    uint16_t readback = 0;

    if (!build_clearbit_patch_branch_instr(&branch_instr, true)) {
        return false;
    }

    current = read_patch_halfword();
    /* NOR flash only supports 1->0.  Verify every target-0 bit is still 1. */
    if ((current & branch_instr) != branch_instr) {
        console_printf(
            "[-] Cannot apply ClearBitPatch without erase. current=0x%04X target=0x%04X\r\n",
            current,
            branch_instr);
        return false;
    }

    write_patch_halfword(branch_instr);

    /* ---- verify ---- */
    readback = read_patch_halfword();
    if (readback == branch_instr) {
        return true;
    }

    console_printf(
        "[!] Verify failed: wrote 0x%04X, read 0x%04X\r\n",
        branch_instr, readback);

    /* Can further 1->0 fix the mismatch? */
    if ((readback & branch_instr) != branch_instr) {
        console_puts(
            "[!] Unrecoverable: need 0->1 transition. Entering HardFault recovery.\r\n");
        clearbit_hardfault_recovery_trigger(branch_instr, fun2);
        return false;
    }

    /* Still possible via 1->0, retry once */
    console_puts("[!] Retrying write (1->0 still possible)...\r\n");
    write_patch_halfword(branch_instr);
    readback = read_patch_halfword();
    if (readback == branch_instr) {
        console_puts("[+] Retry succeeded.\r\n");
        return true;
    }

    console_puts("[!] Retry failed. Entering HardFault recovery.\r\n");
    clearbit_hardfault_recovery_trigger(branch_instr, fun2);
    return false;
}

void clearbit_patch_unapply(void) {
    write_patch_halfword(CLEARBIT_PATCH_UNPATCH_HALFWORD);
}

bool clearbit_patch_is_active(void) {
    uint16_t branch_instr = 0;

    if (!build_clearbit_patch_branch_instr(&branch_instr, false)) {
        return false;
    }

    return read_patch_halfword() == branch_instr;
}

bool clearbit_patch_demo_can_run(void) {
    return read_patch_halfword() == CLEARBIT_PATCH_ORIGINAL_HALFWORD;
}

void clearbit_patch_print_status(void) {
    uint16_t instr = read_patch_halfword();
    uint16_t branch_instr = 0;
    bool has_branch = build_clearbit_patch_branch_instr(&branch_instr, false);

    console_printf("[ClearBitPatch] patch_slot first halfword: 0x%04X\r\n", instr);

    if (has_branch && instr == branch_instr) {
        console_puts("[ClearBitPatch] mode: redirect to fun2\r\n");
    } else if (instr == CLEARBIT_PATCH_ORIGINAL_HALFWORD) {
        console_puts("[ClearBitPatch] mode: original entry (fun1 path)\r\n");
    } else if (instr == CLEARBIT_PATCH_UNPATCH_HALFWORD) {
        console_puts("[ClearBitPatch] mode: unpatched forward jump (fun1 path)\r\n");
    } else {
        console_puts("[ClearBitPatch] mode: unknown\r\n");
    }
}

__attribute__((naked, noinline, used, section(".hotpatch_page.slot"), aligned(2)))
int patch_slot(void) {
    __asm volatile(
        ".thumb        \n"
        ".hword 0xE7FF \n"
        "nop           \n"
        "b   fun1      \n");
}

memory_cost_t clearbit_patch_memory_cost(void) {
    memory_cost_t cost;

    cost.flash_bytes = (uint32_t)((uintptr_t)&__hotpatch_page_end__
                                - (uintptr_t)&__hotpatch_page_start__);
    cost.ram_bytes   = (uint32_t)(sizeof(g_patch_recovery_target)
                                + sizeof(g_patch_recovery_instr)
                                + sizeof(g_patch_recovery_pending));
    return cost;
}
