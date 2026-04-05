#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void console_init(void);
void console_puts(const char *s);
int console_printf(const char *format, ...);
void console_prompt(void);
bool console_poll_line(char *buf, size_t buf_size, size_t *len);
bool console_prompt_u32(const char *prompt, uint32_t *out);
bool prompt_rtt_u32(const char *prompt, uint32_t *out);

void console_print_help(void);
void console_print_status(void);
void console_run_startup_smoke_test(void);
void console_handle_command(const char *cmd);

#endif
