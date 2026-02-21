#ifndef BASE_MODULE_H
#define BASE_MODULE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h> 
#include "nrf.h"

/*
 * This module is intended for bare-metal nRF52840 (no SoftDevice).
 * If you are using SoftDevice, replace direct NVMC ops with sd_flash_* APIs.
 */

/* ================= Core constants ================= */
#define BASE_UDF_HW             (0xDE42u)   /* UDF #imm8 in Thumb (generally causes UsageFault) */
#define BXLR_HW                 (0x4770u)   /* bx lr */
#define PC_THUMB_BIT            (1u)

/* "Safe-ish" Thumb-1 ranges used by the benchmark filter */
#define THUMB_SHIFT_ADD_LIMIT   (0x2000u)
#define THUMB_DATA_PROC_MASK    (0xFC00u)
#define THUMB_DATA_PROC_BASE    (0x4000u)

/* Optional: avoid clashing with libc printf by not exporting a strong 'printf' */
#ifndef BASE_OVERRIDE_PRINTF
#define BASE_OVERRIDE_PRINTF 0
#endif

/* ================= RTT logger ================= */
void rtt_putc(char c);
void rtt_puts(const char *s);
void rtt_put_u32(uint32_t v);
void rtt_put_hex16(uint16_t v);
void rtt_put_hex32(uint32_t v);
int  rtt_printf(const char *fmt, ...);

#if BASE_OVERRIDE_PRINTF
#define printf rtt_printf
#endif

/* ================= Hardware timer (DWT CYCCNT) ================= */
void     dwt_init(void);
uint32_t cyc_now(void);
uint32_t cycles_to_us(uint32_t cycles);

/* ================= Barriers ================= */
static inline void memory_barrier(void) { __DSB(); __ISB(); }

/* ================= Flash + slot management ================= */
uint32_t flash_page_size(void);
uint32_t flash_total_size(void);

/* Default bench page = last flash page (must be reserved in linker/placement!). */
uint32_t bench_page_addr(void);

uint16_t slot_read_hw(uint32_t page_addr);
bool     slot_set_udf_with_erase(uint32_t page_addr);
bool     slot_set_hw_with_erase(uint32_t page_addr, uint16_t hw);

/* Execute the slot as Thumb code: [hw][bx lr] */
int execute_target_slot(uint32_t page_addr);

#endif /* BASE_MODULE_H */
