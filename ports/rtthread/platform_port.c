#include "ports/common/platform_port.h"

void platform_console_init(void) {
    /* TODO: bind console startup to the RT-Thread shell/backend. */
}

void platform_console_write(const char *text) {
    (void)text;
    /* TODO: preserve current output strings while routing through RT-Thread I/O. */
}

int platform_console_vprintf(const char *format, va_list *args) {
    (void)format;
    (void)args;
    /* TODO: route formatted output through the RT-Thread console backend. */
    return 0;
}

void platform_command_prompt(void) {
    /* TODO: align prompt handling with the RT-Thread shell path. */
}

bool platform_command_poll_line(char *buf, size_t buf_size, size_t *len) {
    (void)buf;
    (void)buf_size;
    (void)len;
    /* TODO: read a command line from the RT-Thread shell/backend. */
    return false;
}

bool platform_command_prompt_u32(const char *prompt, uint32_t *out) {
    (void)prompt;
    (void)out;
    /* TODO: support the interactive numeric prompts needed by the current demos. */
    return false;
}

int platform_patch_call(platform_patch_entry_fn_t entry) {
    return (entry != NULL) ? entry() : 0;
}

void platform_lock_patch_region(void) {
    /* TODO: serialize MorphPatch flash writes with RT-Thread synchronization. */
}

void platform_unlock_patch_region(void) {
    /* TODO: release the RT-Thread patch-region guard. */
}

void platform_victim_suspend(void) {
    /* TODO: suspend only the victim thread while patching. */
}

void platform_victim_resume(void) {
    /* TODO: resume the victim thread after patch apply/unapply. */
}
