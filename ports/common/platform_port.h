#ifndef PLATFORM_PORT_H
#define PLATFORM_PORT_H

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*platform_patch_entry_fn_t)(void);

void platform_console_init(void);
void platform_console_write(const char *text);
int platform_console_vprintf(const char *format, va_list *args);

void platform_command_prompt(void);
bool platform_command_poll_line(char *buf, size_t buf_size, size_t *len);
bool platform_command_prompt_u32(const char *prompt, uint32_t *out);

int platform_patch_call(platform_patch_entry_fn_t entry);
void platform_lock_patch_region(void);
void platform_unlock_patch_region(void);
void platform_victim_suspend(void);
void platform_victim_resume(void);

#endif
