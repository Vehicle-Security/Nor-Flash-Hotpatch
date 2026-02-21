#ifndef BASE_MODULE_H
#define BASE_MODULE_H

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "nrf.h"

/*
 * 该模块面向 nRF52840 裸机环境（不使用 SoftDevice）。
 * 如果工程启用了 SoftDevice，请把直接 NVMC 操作替换为 sd_flash_* 接口。
 */

/* ================= 核心常量 ================= */
#define BASE_UDF_HW             (0xDE42u) /* Thumb UDF #imm8，通常触发 UsageFault */
#define BXLR_HW                 (0x4770u) /* bx lr */
#define PC_THUMB_BIT            (1u)

/* Benchmark 中用于过滤“相对安全”目标指令的 Thumb-1 范围 */
#define THUMB_SHIFT_ADD_LIMIT   (0x2000u)
#define THUMB_DATA_PROC_MASK    (0xFC00u)
#define THUMB_DATA_PROC_BASE    (0x4000u)

/* 可选：避免与 libc 的 printf 冲突 */
#ifndef BASE_OVERRIDE_PRINTF
#define BASE_OVERRIDE_PRINTF    0
#endif

/* ================= RTT 输出 ================= */
void rtt_putc(char c);
void rtt_puts(const char *s);
void rtt_put_u32(uint32_t v);
void rtt_put_hex16(uint16_t v);
void rtt_put_hex32(uint32_t v);
int rtt_printf(const char *fmt, ...);

#if BASE_OVERRIDE_PRINTF
#define printf rtt_printf
#endif

/* ================= 硬件计时（DWT CYCCNT） ================= */
void dwt_init(void);
uint32_t cyc_now(void);
uint32_t cycles_to_us(uint32_t cycles);

/* ================= 屏障 ================= */
static inline void memory_barrier(void)
{
    __DSB();
    __ISB();
}

/* ================= Flash 与 slot 管理 ================= */
uint32_t flash_page_size(void);
uint32_t flash_total_size(void);

/* 默认使用最后一页做 benchmark，链接脚本中需要提前预留。 */
uint32_t bench_page_addr(void);

uint16_t slot_read_hw(uint32_t page_addr);
bool slot_set_udf_with_erase(uint32_t page_addr);
bool slot_set_hw_with_erase(uint32_t page_addr, uint16_t hw);

/* 按 Thumb 代码执行 slot：布局是 [hw][bx lr] */
int execute_target_slot(uint32_t page_addr);

#endif /* BASE_MODULE_H */
