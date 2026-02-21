#include "base_module.h"
#include "SEGGER_RTT.h"

#include <stddef.h>

/* ================= RTT 输出 ================= */
void rtt_putc(char c)
{
    SEGGER_RTT_PutChar(0, c);
}

void rtt_puts(const char *s)
{
    if (s == NULL) {
        return;
    }

    SEGGER_RTT_WriteString(0, s);
}

void rtt_put_u32(uint32_t v)
{
    char buf[11];
    int write_pos = 10;

    buf[write_pos] = '\0';

    if (v == 0u) {
        write_pos--;
        buf[write_pos] = '0';
        rtt_puts(&buf[write_pos]);
        return;
    }

    while (v > 0u && write_pos > 0) {
        uint32_t digit = v % 10u;
        write_pos--;
        buf[write_pos] = (char)('0' + digit);
        v /= 10u;
    }

    rtt_puts(&buf[write_pos]);
}

void rtt_put_hex16(uint16_t v)
{
    static const char hex[] = "0123456789ABCDEF";

    rtt_puts("0x");
    for (int i = 3; i >= 0; --i) {
        rtt_putc(hex[(v >> (i * 4)) & 0xFu]);
    }
}

void rtt_put_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";

    rtt_puts("0x");
    for (int i = 7; i >= 0; --i) {
        rtt_putc(hex[(v >> (i * 4)) & 0xFu]);
    }
}

int rtt_printf(const char *fmt, ...)
{
    if (fmt == NULL) {
        return 0;
    }

    va_list ap;
    va_start(ap, fmt);

    for (const char *cursor = fmt; *cursor != '\0'; ++cursor) {
        if (*cursor != '%') {
            rtt_putc(*cursor);
            continue;
        }

        cursor++;
        if (*cursor == '\0') {
            rtt_putc('%');
            break;
        }

        if (*cursor == '%') {
            rtt_putc('%');
            continue;
        }

        if (*cursor == 'c') {
            char ch = (char)va_arg(ap, int);
            rtt_putc(ch);
            continue;
        }

        if (*cursor == 's') {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) {
                rtt_puts("(null)");
            } else {
                rtt_puts(s);
            }
            continue;
        }

        if (*cursor == 'u') {
            uint32_t v = va_arg(ap, uint32_t);
            rtt_put_u32(v);
            continue;
        }

        /* 不支持的格式占位符，按原样输出，便于排查 */
        rtt_putc('%');
        rtt_putc(*cursor);
    }

    va_end(ap);
    return 0;
}

/* ================= DWT 计时 ================= */
void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    memory_barrier();
}

uint32_t cyc_now(void)
{
    return DWT->CYCCNT;
}

uint32_t cycles_to_us(uint32_t cycles)
{
    uint32_t hz = SystemCoreClock;
    if (hz == 0u) {
        return 0u;
    }

    return (uint32_t)(((uint64_t)cycles * 1000000ull) / (uint64_t)hz);
}

/* ================= Flash HAL（直接 NVMC，不依赖 SoftDevice） ================= */
static inline void nvmc_wait_ready(void)
{
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
    }
}

/*
 * 如果平台/链接脚本支持，把这些函数放到 RAM 执行可降低擦写期间 flash 取指风险。
 * 当前默认保持在 .text。
 */
#ifndef BASE_RAMFUNC
#define BASE_RAMFUNC __attribute__((noinline))
#endif

BASE_RAMFUNC static void nvmc_write_word(uint32_t addr, uint32_t value)
{
    __disable_irq();
    nvmc_wait_ready();

    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    nvmc_wait_ready();

    *(volatile uint32_t *)addr = value;
    nvmc_wait_ready();

    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    nvmc_wait_ready();
    __enable_irq();

    memory_barrier();

#ifdef NVMC_ICACHECNF_CACHEEN_Msk
    /* nRF52840 带 NVMC I-cache，写 flash 后主动切换一次，避免读到旧指令 */
    NRF_NVMC->ICACHECNF = (NVMC_ICACHECNF_CACHEEN_Disabled << NVMC_ICACHECNF_CACHEEN_Pos);
    memory_barrier();
    NRF_NVMC->ICACHECNF = (NVMC_ICACHECNF_CACHEEN_Enabled << NVMC_ICACHECNF_CACHEEN_Pos);
    memory_barrier();
#endif
}

BASE_RAMFUNC static void nvmc_erase_page(uint32_t page_addr)
{
    __disable_irq();
    nvmc_wait_ready();

    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
    nvmc_wait_ready();

    NRF_NVMC->ERASEPAGE = page_addr;
    nvmc_wait_ready();

    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    nvmc_wait_ready();
    __enable_irq();

    memory_barrier();

#ifdef NVMC_ICACHECNF_CACHEEN_Msk
    NRF_NVMC->ICACHECNF = (NVMC_ICACHECNF_CACHEEN_Disabled << NVMC_ICACHECNF_CACHEEN_Pos);
    memory_barrier();
    NRF_NVMC->ICACHECNF = (NVMC_ICACHECNF_CACHEEN_Enabled << NVMC_ICACHECNF_CACHEEN_Pos);
    memory_barrier();
#endif
}

uint32_t flash_page_size(void)
{
    return NRF_FICR->CODEPAGESIZE;
}

uint32_t flash_total_size(void)
{
    return NRF_FICR->CODEPAGESIZE * NRF_FICR->CODESIZE;
}

uint32_t bench_page_addr(void)
{
    /* nRF52 的 flash 基地址是 0x00000000 */
    return flash_total_size() - flash_page_size();
}

static inline uint32_t slot_make_word(uint16_t hw)
{
    /*
     * 小端布局：
     * [addr+0]: hw（低半字）
     * [addr+2]: bx lr（高半字）
     */
    return ((uint32_t)BXLR_HW << 16) | (uint32_t)hw;
}

uint16_t slot_read_hw(uint32_t page_addr)
{
    return (uint16_t)(*(volatile uint32_t *)page_addr & 0xFFFFu);
}

bool slot_set_udf_with_erase(uint32_t page_addr)
{
    nvmc_erase_page(page_addr);
    nvmc_write_word(page_addr, slot_make_word((uint16_t)BASE_UDF_HW));
    return slot_read_hw(page_addr) == (uint16_t)BASE_UDF_HW;
}

bool slot_set_hw_with_erase(uint32_t page_addr, uint16_t hw)
{
    nvmc_erase_page(page_addr);
    nvmc_write_word(page_addr, slot_make_word(hw));
    return slot_read_hw(page_addr) == hw;
}

typedef int (*TargetInstructionFunc_t)(void);

int execute_target_slot(uint32_t page_addr)
{
    TargetInstructionFunc_t target = (TargetInstructionFunc_t)(page_addr | PC_THUMB_BIT);
    return target();
}
