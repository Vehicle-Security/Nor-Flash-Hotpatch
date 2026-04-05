#include "board_port.h"

#include <stdarg.h>
#include <stdio.h>

#include "ch32v20x.h"

static bool s_console_initialized = false;

enum {
    BOARD_CONSOLE_BAUDRATE = 115200u,
    BOARD_FLASH_BYTES = 64u * 1024u,
    BOARD_FLASH_PAGE_BYTES = 4u * 1024u,
    BOARD_FLASH_PAGE_WORDS = BOARD_FLASH_PAGE_BYTES / sizeof(uint32_t),
};

typedef struct {
    uintptr_t program_halfword_addr;
    uintptr_t verify_halfword_addr;
    uint16_t old_halfword;
    uint16_t new_halfword;
} board_patch_halfword_write_t;

static void board_console_write_char(char ch)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) {
    }

    USART_SendData(USART1, (uint8_t)ch);
}

static bool board_is_flash_address(uintptr_t addr)
{
    const uintptr_t flash_start = (uintptr_t)FLASH_BASE;
    const uintptr_t flash_end = flash_start + (uintptr_t)BOARD_FLASH_BYTES;

    return (addr >= flash_start) && ((addr + sizeof(uint32_t)) <= flash_end);
}

static bool board_resolve_flash_addresses(uintptr_t addr, uintptr_t *program_addr, uintptr_t *verify_addr)
{
    if ((program_addr == NULL) || (verify_addr == NULL)) {
        return false;
    }

    /* Code executes from the 0x00000000 alias, but flash programming uses 0x08000000. */
    if (addr <= ((uintptr_t)BOARD_FLASH_BYTES - sizeof(uint32_t))) {
        *program_addr = (uintptr_t)FLASH_BASE + addr;
        *verify_addr = addr;
        return true;
    }

    if (board_is_flash_address(addr)) {
        *program_addr = addr;
        *verify_addr = addr - (uintptr_t)FLASH_BASE;
        return true;
    }

    return false;
}

static uint32_t s_flash_page_buffer[BOARD_FLASH_PAGE_WORDS];

bool board_console_init(void)
{
    if (!s_console_initialized) {
        GPIO_InitTypeDef gpio_init = {0};
        USART_InitTypeDef usart_init = {0};

        SystemCoreClockUpdate();

        RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

        gpio_init.GPIO_Pin = GPIO_Pin_9;
        gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
        gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
        GPIO_Init(GPIOA, &gpio_init);

        gpio_init.GPIO_Pin = GPIO_Pin_10;
        gpio_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(GPIOA, &gpio_init);

        USART_StructInit(&usart_init);
        usart_init.USART_BaudRate = BOARD_CONSOLE_BAUDRATE;
        usart_init.USART_WordLength = USART_WordLength_8b;
        usart_init.USART_StopBits = USART_StopBits_1;
        usart_init.USART_Parity = USART_Parity_No;
        usart_init.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
        usart_init.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
        USART_Init(USART1, &usart_init);
        USART_Cmd(USART1, ENABLE);

        s_console_initialized = true;
    }

    return s_console_initialized;
}

void board_console_write(const char *s)
{
    if (s == NULL) {
        return;
    }

    (void)board_console_init();

    while (*s != '\0') {
        board_console_write_char(*s);
        ++s;
    }
}

void board_console_printf(const char *fmt, ...)
{
    char buffer[256];
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written <= 0) {
        return;
    }

    buffer[sizeof(buffer) - 1u] = '\0';
    board_console_write(buffer);
}

int board_console_getchar_nonblock(void)
{
    (void)board_console_init();

    if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET) {
        return -1;
    }

    return (int)(USART_ReceiveData(USART1) & 0xFFu);
}

void board_console_putchar(char c)
{
    (void)board_console_init();
    board_console_write_char(c);
}

static bool board_patch_prepare_halfword_write(uintptr_t addr,
                                               uint32_t oldv,
                                               uint32_t newv,
                                               board_patch_halfword_write_t *write_desc)
{
    uintptr_t program_addr = 0u;
    uintptr_t verify_addr = 0u;
    volatile uint32_t *word_ptr;
    uint16_t old_halfwords[2];
    uint16_t new_halfwords[2];
    uint32_t i;
    uint32_t changed_index = 0u;
    bool changed_found = false;

    if ((write_desc == NULL) ||
        ((addr & 0x3u) != 0u) ||
        !board_resolve_flash_addresses(addr, &program_addr, &verify_addr)) {
        return false;
    }

    if ((newv & ~oldv) != 0u) {
        return false;
    }

    word_ptr = (volatile uint32_t *)verify_addr;
    if (*word_ptr != oldv) {
        return false;
    }

    old_halfwords[0] = (uint16_t)(oldv & 0xFFFFu);
    old_halfwords[1] = (uint16_t)(oldv >> 16);
    new_halfwords[0] = (uint16_t)(newv & 0xFFFFu);
    new_halfwords[1] = (uint16_t)(newv >> 16);

    for (i = 0u; i < 2u; ++i) {
        if (old_halfwords[i] == new_halfwords[i]) {
            continue;
        }

        if (changed_found || ((uint16_t)(new_halfwords[i] & (uint16_t)(~old_halfwords[i])) != 0u)) {
            return false;
        }

        changed_index = i;
        changed_found = true;
    }

    if (!changed_found) {
        return false;
    }

    write_desc->program_halfword_addr = program_addr + (changed_index * sizeof(uint16_t));
    write_desc->verify_halfword_addr = verify_addr + (changed_index * sizeof(uint16_t));
    write_desc->old_halfword = old_halfwords[changed_index];
    write_desc->new_halfword = new_halfwords[changed_index];

    if (*(const volatile uint16_t *)write_desc->verify_halfword_addr != write_desc->old_halfword) {
        return false;
    }

    return true;
}

/*
 * CH32V20x startup in this project does not currently relocate .highcode into SRAM,
 * so true SRAM-exec is intentionally not enabled here to avoid widening changes into startup/linker code.
 */
static FLASH_Status board_flash_program_halfword_minimal(uintptr_t program_halfword_addr, uint16_t new_halfword)
{
    return FLASH_ProgramHalfWord((uint32_t)program_halfword_addr, new_halfword);
}

bool board_patch_write_word_monotonic(uintptr_t addr, uint32_t oldv, uint32_t newv)
{
    board_patch_halfword_write_t write_desc = {0};
    FLASH_Status flash_status = FLASH_TIMEOUT;
    bool wrote_ok = false;

    if (!board_patch_prepare_halfword_write(addr, oldv, newv, &write_desc)) {
        return false;
    }

    __disable_irq();

    FLASH_Unlock();
    flash_status = board_flash_program_halfword_minimal(write_desc.program_halfword_addr, write_desc.new_halfword);
    FLASH_Lock();
    __enable_irq();

    board_code_sync();
    wrote_ok = (flash_status == FLASH_COMPLETE) &&
               (*(const volatile uint16_t *)write_desc.verify_halfword_addr == write_desc.new_halfword);

    if (!wrote_ok) {
        const uintptr_t verify_word_addr = write_desc.verify_halfword_addr & ~(uintptr_t)0x3u;
        const uint32_t flash_statr = FLASH->STATR;
        const uint32_t flash_ctlr = FLASH->CTLR;
        const uint32_t flash_addr = FLASH->ADDR;
        const uint32_t actual_word = *(const volatile uint32_t *)verify_word_addr;

        board_console_printf(
            "[patch-debug] flash_status=%d statr=0x%08lX ctlr=0x%08lX addr=0x%08lX actual=0x%08lX expected=0x%08lX hwoff=0x%lX actual_hw=0x%04lX expected_hw=0x%04lX\r\n",
            (int)flash_status,
            (unsigned long)flash_statr,
            (unsigned long)flash_ctlr,
            (unsigned long)flash_addr,
            (unsigned long)actual_word,
            (unsigned long)newv,
            (unsigned long)(write_desc.program_halfword_addr & 0x2u),
            (unsigned long)(*(const volatile uint16_t *)write_desc.verify_halfword_addr),
            (unsigned long)write_desc.new_halfword);
    }

    return wrote_ok;
}

bool board_patch_write_word_with_erase(uintptr_t addr, uint32_t oldv, uint32_t newv)
{
    uintptr_t program_addr = 0u;
    uintptr_t verify_addr = 0u;
    uintptr_t program_page = 0u;
    uintptr_t verify_page = 0u;
    size_t word_index = 0u;
    size_t i = 0u;
    volatile uint32_t *word_ptr;
    FLASH_Status flash_status = FLASH_TIMEOUT;
    uint32_t flash_statr = 0u;
    uint32_t flash_ctlr = 0u;
    uint32_t flash_addr = 0u;
    bool wrote_ok = false;

    if (((addr & 0x3u) != 0u) || !board_resolve_flash_addresses(addr, &program_addr, &verify_addr)) {
        return false;
    }

    word_ptr = (volatile uint32_t *)verify_addr;
    if (*word_ptr != oldv) {
        return false;
    }

    program_page = program_addr & ~((uintptr_t)BOARD_FLASH_PAGE_BYTES - 1u);
    verify_page = verify_addr & ~((uintptr_t)BOARD_FLASH_PAGE_BYTES - 1u);
    word_index = (size_t)((verify_addr - verify_page) / sizeof(uint32_t));

    for (i = 0u; i < BOARD_FLASH_PAGE_WORDS; ++i) {
        s_flash_page_buffer[i] = *(const volatile uint32_t *)(verify_page + (i * sizeof(uint32_t)));
    }
    s_flash_page_buffer[word_index] = newv;

    __disable_irq();

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
    flash_status = FLASH_ErasePage((uint32_t)program_page);
    if (flash_status == FLASH_COMPLETE) {
        FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
        for (i = 0u; i < BOARD_FLASH_PAGE_WORDS; ++i) {
            const uint32_t word = s_flash_page_buffer[i];

            if (word == 0xFFFFFFFFu) {
                continue;
            }

            flash_status = FLASH_ProgramWord((uint32_t)(program_page + (i * sizeof(uint32_t))), word);
            if (flash_status != FLASH_COMPLETE) {
                break;
            }
        }
    }
    FLASH_Lock();

    flash_statr = FLASH->STATR;
    flash_ctlr = FLASH->CTLR;
    flash_addr = FLASH->ADDR;
    __enable_irq();

    board_code_sync();
    wrote_ok = (flash_status == FLASH_COMPLETE) && (*word_ptr == newv);

    if (!wrote_ok) {
        board_console_printf(
            "[patch-debug] erase-write status=%d page=0x%08lX slot=0x%08lX actual=0x%08lX expected=0x%08lX statr=0x%08lX ctlr=0x%08lX addr=0x%08lX\r\n",
            (int)flash_status,
            (unsigned long)program_page,
            (unsigned long)program_addr,
            (unsigned long)(*word_ptr),
            (unsigned long)newv,
            (unsigned long)flash_statr,
            (unsigned long)flash_ctlr,
            (unsigned long)flash_addr);
    }

    return wrote_ok;
}

void board_code_sync(void)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
    __asm__ volatile("fence.i" ::: "memory");
}
