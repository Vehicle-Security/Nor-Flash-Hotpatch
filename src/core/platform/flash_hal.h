/*
 * flash_hal.h — Platform-independent flash write abstraction for MorphPatch.
 *
 * Each target (nRF52840, STM32F4, STM32F1) provides its own implementation
 * of these two functions in targets/<target>/platform/flash_hal_*.c
 */
#ifndef FLASH_HAL_H
#define FLASH_HAL_H

#include <stdint.h>

/*
 * Write a 32-bit word to flash at a word-aligned address.
 * The implementation must:
 *   1. Disable interrupts
 *   2. Unlock flash controller
 *   3. Perform the write
 *   4. Wait for completion
 *   5. Lock flash controller
 *   6. Re-enable interrupts
 */
void flash_hal_write_word(uintptr_t aligned_addr, uint32_t new_val);

/*
 * Invalidate instruction cache after a flash write so the CPU fetches
 * the updated code.  Called after write + DSB/ISB barriers.
 */
void flash_hal_invalidate_icache(void);

#endif
