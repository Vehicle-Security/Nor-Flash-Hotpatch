#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "SEGGER_RTT.h"
#include "bsp.h"
#include "nrf.h"

void patch_slot(void);
void fun2(void);

static uintptr_t patch_slot_addr(void) {
    return ((uintptr_t)patch_slot) & ~(uintptr_t)1u;
}

static void console_init(void) {
    SEGGER_RTT_Init();
    SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);
    SEGGER_RTT_WriteString(0, "\r\nRTT console ready. (Erase-less Version)\r\n");
}

static void console_puts(const char *s) {
    SEGGER_RTT_WriteString(0, s);
}

static void console_prompt(void) {
    console_puts("rtt> ");
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

static uint16_t patch_branch_instr(void) {
    uintptr_t from = patch_slot_addr();
    uintptr_t to = ((uintptr_t)fun2) & ~(uintptr_t)1u;
    int32_t diff = (int32_t)to - (int32_t)(from + 4u);
    uint16_t imm11 = (uint16_t)(((uint32_t)(diff >> 1)) & 0x07FFu);

    return (uint16_t)(0xE000u | imm11);
}

static void write_patch_halfword(uint16_t instr) {
    uintptr_t patch_addr = patch_slot_addr();
    uint32_t aligned_addr = (uint32_t)(patch_addr & ~(uintptr_t)3u);
    volatile uint32_t *flash_word = (volatile uint32_t *)aligned_addr;
    
    uint32_t old_val = *flash_word;
    uint32_t new_val;

    if ((patch_addr & 2u) == 0u) {
        new_val = (old_val & 0xFFFF0000u) | instr;
    } else {
        new_val = (old_val & 0x0000FFFFu) | ((uint32_t)instr << 16);
    }

    NRF_NVMC->CONFIG = 1;
    while (NRF_NVMC->READY == 0) {
    }

    *flash_word = new_val;
    while (NRF_NVMC->READY == 0) {
    }

    NRF_NVMC->CONFIG = 0;
    while (NRF_NVMC->READY == 0) {
    }

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

static void patch_slot_redirect_to_fun2(void) {
    write_patch_halfword(patch_branch_instr());
}

static void patch_slot_unpatch_to_fun1(void) {
    write_patch_halfword(0xE000u);
}

static void print_patch_status(void) {
    uint16_t instr = read_patch_halfword();
    uint16_t branch = patch_branch_instr();

    SEGGER_RTT_printf(0, "patch_slot first halfword: 0x%04X\r\n", instr);
    if (instr == branch) {
        console_puts("mode: redirect to fun2\r\n");
    } else if (instr == 0xE7FFu) {
        console_puts("mode: original entry (fun1 path)\r\n");
    } else if (instr == 0xE000u) {
        console_puts("mode: unpatched forward jump (fun1 path)\r\n");
    } else {
        console_puts("mode: unknown\r\n");
    }
}

void fun1(void) {
    console_puts("this is fun1\r\n");
}

__attribute__((naked, noinline, used, section(".hotpatch_page"), aligned(2)))
void patch_slot(void) {
    __asm volatile(
        ".thumb                \n"
        ".hword 0xE7FF         \n"
        "nop                   \n"
        "push {lr}             \n"
        "bl   fun1             \n"
        "pop  {pc}             \n");
}

__attribute__((noinline, section(".hotpatch_page"))) void fun2(void) {
    console_puts("this is fun2\r\n");
}

static void run_demo(void) {
    console_puts("--- Demo Start ---\r\n");
    console_puts("1. Initial call:\r\n");
    patch_slot();

    console_puts("2. Applying patch...\r\n");
    patch_slot_redirect_to_fun2();
    patch_slot();

    console_puts("3. Unpatching...\r\n");
    patch_slot_unpatch_to_fun1();
    patch_slot();
    console_puts("--- Demo End ---\r\n");
}

static void print_help(void) {
    // 移除了 reset 指令的提示
    console_puts("commands: help, demo, call, patch, unpatch, status\r\n");
}

static void handle_command(const char *cmd) {
    if (cmd[0] == '\0') {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        print_help();
        return;
    }

    if (strcmp(cmd, "demo") == 0) {
        run_demo();
        return;
    }

    if (strcmp(cmd, "call") == 0) {
        patch_slot();
        return;
    }

    if (strcmp(cmd, "patch") == 0) {
        patch_slot_redirect_to_fun2();
        print_patch_status();
        return;
    }

    if (strcmp(cmd, "unpatch") == 0) {
        patch_slot_unpatch_to_fun1();
        print_patch_status();
        return;
    }

    if (strcmp(cmd, "reset") == 0) {
        console_puts("Error: 'reset' requires Flash erase. Erase operations are removed in this version.\r\n");
        return;
    }

    if (strcmp(cmd, "status") == 0) {
        print_patch_status();
        return;
    }

    SEGGER_RTT_printf(0, "unknown command: %s\r\n", cmd);
}

int main(void) {
    char cmd[32];
    size_t len = 0;

    bsp_board_init(BSP_INIT_LEDS);
    console_init();

    print_help();
    console_prompt();

    while (true) {
        int key = SEGGER_RTT_GetKey();

        if (key < 0) {
            continue;
        }

        if (key == '\r' || key == '\n') {
            console_puts("\r\n");
            cmd[len] = '\0';
            handle_command(cmd);
            len = 0;
            console_prompt();
            continue;
        }

        if ((key == '\b' || key == 0x7F) && len > 0u) {
            len--;
            console_puts("\b \b");
            continue;
        }

        if (key >= 0x20 && key <= 0x7E && len < (sizeof(cmd) - 1u)) {
            cmd[len++] = (char)key;
            SEGGER_RTT_PutChar(0, (char)key);
        }
    }
}