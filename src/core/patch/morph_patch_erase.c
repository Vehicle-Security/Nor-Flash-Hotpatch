/*
 * morph_patch_erase.c — Erase-rewrite (inline-hook) scheme for nRF52840.
 *
 * Provides a second patch slot (patch_slot_erase) that uses page erase +
 * rewrite to install/uninstall patches.
 *
 * Because the erase slot lives on a different 4KB page from fun2, the 16-bit
 * Thumb B.N (±2KB) cannot reach.  We use the 32-bit Thumb B.W (±16MB) instead,
 * which occupies a full aligned word (two consecutive halfwords).
 */
#include "morph_patch_erase.h"

#include <stdint.h>

#include "nrf.h"

#include "../console/console.h"
#include "../target/targets.h"

/*
 * Factory state: 0xE7FF 0xBF00  (b.n +2 ; nop) — falls through to fun1
 * Patched state: B.W fun2 (Thumb-2 32-bit branch)
 * Unpatched:     0xE000 0xBF00  (b.n +0 ; nop) — infinite-loops then fun1
 */
#define ERASE_FACTORY_WORD   0xBF00E7FFu  /* b.n +2 ; nop */
#define ERASE_UNPATCH_WORD   0xBF00E000u  /* b.n +0 ; nop */
#define NRF52_PAGE_SIZE      4096u

static uintptr_t erase_slot_addr(void)
{
    return ((uintptr_t)patch_slot_erase) & ~(uintptr_t)1u;
}

static uint32_t erase_read_word(void)
{
    uintptr_t addr = erase_slot_addr();
    uint32_t aligned = (uint32_t)(addr & ~(uintptr_t)3u);
    return *(volatile uint32_t *)aligned;
}

static void invalidate_code_cache(void)
{
#if defined(NVMC_FEATURE_CACHE_PRESENT)
    uint32_t icache = NRF_NVMC->ICACHECNF;
    NRF_NVMC->ICACHECNF = (icache & ~NVMC_ICACHECNF_CACHEEN_Msk);
    __DSB(); __ISB();
    NRF_NVMC->ICACHECNF = icache;
    __DSB(); __ISB();
#endif
}

static void erase_page_and_rewrite_word(uintptr_t slot_addr, uint32_t new_word)
{
    uint32_t page_addr = (uint32_t)(slot_addr & ~(NRF52_PAGE_SIZE - 1u));
    static uint32_t page_buf[NRF52_PAGE_SIZE / sizeof(uint32_t)];
    uint32_t slot_aligned = (uint32_t)(slot_addr & ~(uintptr_t)3u);
    uint32_t word_index = (slot_aligned - page_addr) / sizeof(uint32_t);
    uint32_t i;

    /* 1. Read entire 4KB page */
    for (i = 0u; i < (NRF52_PAGE_SIZE / sizeof(uint32_t)); ++i) {
        page_buf[i] = ((volatile uint32_t *)page_addr)[i];
    }

    /* 2. Modify target word */
    page_buf[word_index] = new_word;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* 3. Erase page */
    NRF_NVMC->CONFIG = 2;
    while (NRF_NVMC->READY == 0) {}
    NRF_NVMC->ERASEPAGE = page_addr;
    while (NRF_NVMC->READY == 0) {}

    /* 4. Rewrite page */
    NRF_NVMC->CONFIG = 1;
    while (NRF_NVMC->READY == 0) {}
    for (i = 0u; i < (NRF52_PAGE_SIZE / sizeof(uint32_t)); ++i) {
        if (page_buf[i] != 0xFFFFFFFFu) {
            ((volatile uint32_t *)page_addr)[i] = page_buf[i];
            while (NRF_NVMC->READY == 0) {}
        }
    }

    NRF_NVMC->CONFIG = 0;
    while (NRF_NVMC->READY == 0) {}
    __set_PRIMASK(primask);

    __DSB(); __ISB();
    invalidate_code_cache();
}

/*
 * Build a Thumb-2 B.W (32-bit unconditional branch) instruction.
 * Encoding: hw1 = 0xF000 | S:imm10, hw2 = 0x9000 | J1:J2:imm11
 * Range: ±16MB.  Stored little-endian as: hw1 | (hw2 << 16).
 */
static bool build_bw_branch_word(uint32_t *out_word)
{
    uintptr_t from = erase_slot_addr();
    uintptr_t to = ((uintptr_t)fun2) & ~(uintptr_t)1u;
    int32_t offset = (int32_t)to - (int32_t)(from + 4u);

    if (offset < -(1 << 24) || offset >= (1 << 24)) {
        return false;
    }

    uint32_t S   = (offset < 0) ? 1u : 0u;
    uint32_t uoff = (uint32_t)offset;
    uint32_t imm10 = (uoff >> 12) & 0x3FFu;
    uint32_t imm11 = (uoff >> 1) & 0x7FFu;
    uint32_t I1  = (uoff >> 23) & 1u;
    uint32_t I2  = (uoff >> 22) & 1u;
    uint32_t J1  = (~(I1 ^ S)) & 1u;
    uint32_t J2  = (~(I2 ^ S)) & 1u;

    uint16_t hw1 = (uint16_t)(0xF000u | (S << 10) | imm10);
    uint16_t hw2 = (uint16_t)(0x9000u | (J1 << 13) | (J2 << 11) | imm11);

    *out_word = (uint32_t)hw1 | ((uint32_t)hw2 << 16);
    return true;
}

int erase_patch_call(void)    { return patch_slot_erase(); }

bool erase_patch_apply(void)
{
    uint32_t branch_word = 0u;

    if (erase_read_word() != ERASE_FACTORY_WORD) {
        return false;
    }

    if (!build_bw_branch_word(&branch_word)) {
        console_puts("[-] erase: fun2 out of B.W range.\r\n");
        return false;
    }

    erase_page_and_rewrite_word(erase_slot_addr(), branch_word);
    return erase_read_word() == branch_word;
}

void erase_patch_unapply(void)
{
    uint32_t current = erase_read_word();
    if (current == ERASE_FACTORY_WORD || current == ERASE_UNPATCH_WORD) {
        return;
    }
    erase_page_and_rewrite_word(erase_slot_addr(), ERASE_UNPATCH_WORD);
}

bool erase_patch_is_active(void)
{
    uint32_t current = erase_read_word();
    uint32_t branch_word = 0u;
    if (!build_bw_branch_word(&branch_word)) return false;
    return current == branch_word;
}

bool erase_patch_demo_can_run(void)
{
    return erase_read_word() == ERASE_FACTORY_WORD;
}

void erase_patch_print_status(void)
{
    uint32_t w = erase_read_word();
    console_printf("[EraseRewrite] slot word: 0x%08lX addr: 0x%08lX\r\n",
                   (unsigned long)w, (unsigned long)erase_slot_addr());
    if (w == ERASE_FACTORY_WORD) {
        console_puts("[EraseRewrite] state: factory (fun1 path)\r\n");
    } else if (erase_patch_is_active()) {
        console_puts("[EraseRewrite] state: patched (fun2 path)\r\n");
    } else if (w == ERASE_UNPATCH_WORD) {
        console_puts("[EraseRewrite] state: unpatched (fun1 path)\r\n");
    } else {
        console_printf("[EraseRewrite] state: unknown (0x%08lX)\r\n", (unsigned long)w);
    }
}
