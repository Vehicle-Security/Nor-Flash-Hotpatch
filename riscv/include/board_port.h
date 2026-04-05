#ifndef BOARD_PORT_H
#define BOARD_PORT_H

#include <stdbool.h>
#include <stdint.h>

bool board_console_init(void);
void board_console_write(const char *s);
void board_console_printf(const char *fmt, ...);
int board_console_getchar_nonblock(void);
void board_console_putchar(char c);

bool board_patch_write_word_monotonic(uintptr_t addr, uint32_t oldv, uint32_t newv);
bool board_patch_write_word_with_erase(uintptr_t addr, uint32_t oldv, uint32_t newv);
void board_code_sync(void);

#endif
