/*
 * flash_hal_nrf52.c — nRF52840 flash write implementation for MorphPatch.
 *
 * Uses NRF_NVMC registers for flash programming and instruction cache control.
 */
#include "core/platform/flash_hal.h"

#include "nrf.h"

void flash_hal_write_word(uintptr_t aligned_addr, uint32_t new_val)
{
    volatile uint32_t *fw = (volatile uint32_t *)aligned_addr;
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
}

void flash_hal_invalidate_icache(void)
{
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
