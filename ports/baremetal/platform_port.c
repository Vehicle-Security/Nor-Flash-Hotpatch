#include "ports/common/platform_port.h"

#include <stdarg.h>

#include "SEGGER_RTT.h"

int SEGGER_RTT_vprintf(unsigned BufferIndex, const char *sFormat, va_list *pParamList);

static int read_key_blocking(void) {
    int key = -1;

    do {
        key = SEGGER_RTT_GetKey();
    } while (key < 0);

    return key;
}

static bool parse_u32_string(const char *text, uint32_t *value) {
    uint32_t parsed = 0u;
    uint32_t base = 10u;
    size_t start = 0u;

    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16u;
        start = 2u;
        if (text[start] == '\0') {
            return false;
        }
    }

    for (size_t i = start; text[i] != '\0'; ++i) {
        uint32_t digit = 0u;

        if (text[i] >= '0' && text[i] <= '9') {
            digit = (uint32_t)(text[i] - '0');
        } else if (base == 16u && text[i] >= 'a' && text[i] <= 'f') {
            digit = (uint32_t)(text[i] - 'a' + 10);
        } else if (base == 16u && text[i] >= 'A' && text[i] <= 'F') {
            digit = (uint32_t)(text[i] - 'A' + 10);
        } else {
            return false;
        }

        parsed = parsed * base + digit;
    }

    *value = parsed;
    return true;
}

void platform_console_init(void) {
    SEGGER_RTT_Init();
    SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_NO_BLOCK_TRIM);
    SEGGER_RTT_WriteString(0, "\r\nRTT console ready. (ClearBitPatch Demo)\r\n");
}

void platform_console_write(const char *text) {
    if (text == NULL) {
        return;
    }

    SEGGER_RTT_WriteString(0, text);
}

int platform_console_vprintf(const char *format, va_list *args) {
    if (format == NULL || args == NULL) {
        return 0;
    }

    return SEGGER_RTT_vprintf(0, format, args);
}

void platform_command_prompt(void) {
    platform_console_write("rtt> ");
}

bool platform_command_poll_line(char *buf, size_t buf_size, size_t *len) {
    int key = 0;

    if (buf == NULL || len == NULL || buf_size == 0u) {
        return false;
    }

    key = SEGGER_RTT_GetKey();
    if (key < 0) {
        return false;
    }

    if (key == '\r' || key == '\n') {
        platform_console_write("\r\n");
        buf[*len] = '\0';
        return true;
    }

    if ((key == '\b' || key == 0x7F) && *len > 0u) {
        (*len)--;
        platform_console_write("\b \b");
        return false;
    }

    if (key >= 0x20 && key <= 0x7E && *len < (buf_size - 1u)) {
        buf[*len] = (char)key;
        (*len)++;
        SEGGER_RTT_PutChar(0, (char)key);
    }

    return false;
}

bool platform_command_prompt_u32(const char *prompt, uint32_t *out) {
    char buf[24];
    size_t len = 0u;

    if (prompt == NULL || out == NULL) {
        return false;
    }

    platform_console_write(prompt);

    while (len < (sizeof(buf) - 1u)) {
        int key = read_key_blocking();

        if (key == '\r' || key == '\n') {
            if (len == 0u) {
                continue;
            }
            break;
        }

        if (key == '\b' || key == 0x7F) {
            if (len > 0u) {
                len--;
                platform_console_write("\b \b");
            }
            continue;
        }

        buf[len] = (char)key;
        len++;
        SEGGER_RTT_PutChar(0, (char)key);
    }

    buf[len] = '\0';
    platform_console_write("\r\n");
    return parse_u32_string(buf, out);
}

int platform_patch_call(platform_patch_entry_fn_t entry) {
    if (entry == NULL) {
        return 0;
    }

    return entry();
}

void platform_lock_patch_region(void) {
}

void platform_unlock_patch_region(void) {
}

void platform_victim_suspend(void) {
}

void platform_victim_resume(void) {
}
