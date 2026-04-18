/*
 * inline_hook_erase.c — Traditional inline-hook cost measurement on STM32F4.
 *
 * Models the classic "patch via sector erase + rewrite" flow used by most
 * vendor SDKs (e.g., the ESP32 targets' board_patch_write_word_erase_rewrite).
 *
 * Flow:
 *   1. Snapshot sector contents into RAM
 *   2. Erase the sector (high-voltage, ~hundreds of ms on NOR)
 *   3. Rewrite the entire sector from the RAM snapshot, word by word
 *   4. Verify
 *
 * Each phase is timed independently with DWT->CYCCNT. Results printed at boot
 * once, so the user can read them off the RTT console.
 *
 * Target sector: STM32F411 sector 2 (0x08008000, 16 KB). Our firmware ends
 * around 0x08007080 (hotpatch_page tail), so sector 2 is safely unused.
 */
#include <stdint.h>
#include <string.h>

#include "stm32f4xx.h"

#include "core/console/console.h"

#define ERASE_BENCH_SECTOR_NUM   2u
#define ERASE_BENCH_SECTOR_ADDR  0x08008000u
#define ERASE_BENCH_SECTOR_SIZE  0x4000u   /* 16 KB */
#define ERASE_BENCH_SECTOR_WORDS (ERASE_BENCH_SECTOR_SIZE / 4u)

static uint32_t g_erase_bench_buf[ERASE_BENCH_SECTOR_WORDS]
    __attribute__((aligned(4)));

static inline void flash_wait_bsy(void) {
    while (FLASH->SR & FLASH_SR_BSY) {}
}

static inline void flash_clear_errors(void) {
    /* Write-1-to-clear all status error flags. Mask covers the bits present
     * across STM32F4 variants: EOP + WRPERR + PGAERR + PGPERR + PGSERR +
     * RDERR (F411 uses this name instead of OPERR). */
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR
              | FLASH_SR_PGPERR | FLASH_SR_PGSERR | FLASH_SR_RDERR;
}

static void flash_unlock(void) {
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }
}

static void flash_lock(void) {
    FLASH->CR |= FLASH_CR_LOCK;
}

/* Erase one sector using PSIZE=10 (32-bit) parallelism. */
static void flash_erase_sector(uint32_t sector_num) {
    flash_wait_bsy();
    flash_clear_errors();

    uint32_t cr = FLASH->CR;
    cr &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
    cr |= FLASH_CR_PSIZE_1;                      /* x32 parallelism */
    cr |= (sector_num << FLASH_CR_SNB_Pos) & FLASH_CR_SNB;
    cr |= FLASH_CR_SER;
    FLASH->CR = cr;
    FLASH->CR |= FLASH_CR_STRT;

    __DSB();
    __ISB();
    (void)FLASH->SR;
    flash_wait_bsy();

    FLASH->CR &= ~FLASH_CR_SER;
}

/* Program a single 32-bit word. Caller must have unlocked flash and cleared
 * PG setup; we manage PG here to mirror a typical SDK-style one-shot write. */
static void flash_program_word(uint32_t addr, uint32_t val) {
    flash_wait_bsy();

    FLASH->CR &= ~FLASH_CR_PSIZE;
    FLASH->CR |= FLASH_CR_PSIZE_1;
    FLASH->CR |= FLASH_CR_PG;

    *(volatile uint32_t *)addr = val;

    __DSB();
    __ISB();
    (void)FLASH->SR;
    flash_wait_bsy();

    FLASH->CR &= ~FLASH_CR_PG;
}

void inline_hook_erase_bench(void) {
    const uint32_t *src = (const uint32_t *)ERASE_BENCH_SECTOR_ADDR;

    /* Phase 0: snapshot sector into RAM (timed separately). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    __DSB();
    __ISB();
    DWT->CYCCNT = 0u;
    __DSB();
    __ISB();

    uint32_t t_start = DWT->CYCCNT;

    for (uint32_t i = 0; i < ERASE_BENCH_SECTOR_WORDS; i++) {
        g_erase_bench_buf[i] = src[i];
    }

    __DSB();
    __ISB();
    uint32_t t_after_read = DWT->CYCCNT;

    flash_unlock();

    __DSB();
    __ISB();
    uint32_t t_before_erase = DWT->CYCCNT;

    flash_erase_sector(ERASE_BENCH_SECTOR_NUM);

    __DSB();
    __ISB();
    uint32_t t_after_erase = DWT->CYCCNT;

    /* Phase 2: rewrite every word from the RAM snapshot. */
    for (uint32_t i = 0; i < ERASE_BENCH_SECTOR_WORDS; i++) {
        flash_program_word(
            ERASE_BENCH_SECTOR_ADDR + (i * 4u),
            g_erase_bench_buf[i]);
    }

    __DSB();
    __ISB();
    uint32_t t_after_rewrite = DWT->CYCCNT;

    flash_lock();

    __set_PRIMASK(primask);

    /* Phase 3: verify readback matches. */
    uint32_t mismatches = 0u;
    for (uint32_t i = 0; i < ERASE_BENCH_SECTOR_WORDS; i++) {
        if (src[i] != g_erase_bench_buf[i]) {
            mismatches++;
        }
    }

    /* Report. */
    uint32_t t_read    = t_after_read    - t_start;
    uint32_t t_erase   = t_after_erase   - t_before_erase;
    uint32_t t_rewrite = t_after_rewrite - t_after_erase;
    uint32_t t_total   = t_after_rewrite - t_start;

    console_puts("\r\n[inline-hook-erase] STM32F4 traditional hook timing\r\n");
    console_printf("  target sector  : %u (0x%08lX, %u KB)\r\n",
                   (unsigned)ERASE_BENCH_SECTOR_NUM,
                   (unsigned long)ERASE_BENCH_SECTOR_ADDR,
                   (unsigned)(ERASE_BENCH_SECTOR_SIZE / 1024u));
    console_printf("  T_snapshot_16K : %10lu cycles  (%lu us)\r\n",
                   (unsigned long)t_read,
                   (unsigned long)(t_read / 100u));
    console_printf("  T_erase        : %10lu cycles  (%lu ms)\r\n",
                   (unsigned long)t_erase,
                   (unsigned long)(t_erase / 100000u));
    console_printf("  T_rewrite_16K  : %10lu cycles  (%lu ms, %u words)\r\n",
                   (unsigned long)t_rewrite,
                   (unsigned long)(t_rewrite / 100000u),
                   (unsigned)ERASE_BENCH_SECTOR_WORDS);
    console_printf("  T_rewrite_word : %10lu cycles  (avg, incl. overhead)\r\n",
                   (unsigned long)(t_rewrite / ERASE_BENCH_SECTOR_WORDS));
    console_printf("  T_total_hook   : %10lu cycles  (%lu ms)\r\n",
                   (unsigned long)t_total,
                   (unsigned long)(t_total / 100000u));
    console_printf("  verify         : %lu mismatches\r\n",
                   (unsigned long)mismatches);
}
