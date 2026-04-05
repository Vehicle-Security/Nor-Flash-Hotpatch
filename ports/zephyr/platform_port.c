#include "ports/common/platform_port.h"

void platform_console_init(void) {
    /* TODO: initialize the Zephyr-side console or shell backend. */
}

void platform_console_write(const char *text) {
    (void)text;
    /* TODO: preserve current strings while routing output through Zephyr. */
}

int platform_console_vprintf(const char *format, va_list *args) {
    (void)format;
    (void)args;
    /* TODO: wire formatted output to printk/shell_fprintf/RTT as chosen later. */
    return 0;
}

void platform_command_prompt(void) {
    /* TODO: map prompt behavior to the chosen Zephyr shell flow. */
}

bool platform_command_poll_line(char *buf, size_t buf_size, size_t *len) {
    (void)buf;
    (void)buf_size;
    (void)len;
    /* TODO: replace with Zephyr shell or backend command ingestion. */
    return false;
}

bool platform_command_prompt_u32(const char *prompt, uint32_t *out) {
    (void)prompt;
    (void)out;
    /* TODO: keep the current interactive demos working through Zephyr shell input. */
    return false;
}

int platform_patch_call(platform_patch_entry_fn_t entry) {
    return (entry != NULL) ? entry() : 0;
}

void platform_lock_patch_region(void) {
    /* TODO: serialize patch apply/unapply with a Zephyr synchronization primitive. */
}

void platform_unlock_patch_region(void) {
    /* TODO: release the Zephyr patch-region guard. */
}

void platform_victim_suspend(void) {
    /* TODO: suspend only the victim thread around patch apply/unapply. */
}

void platform_victim_resume(void) {
    /* TODO: resume the victim thread after patch apply/unapply. */
}
