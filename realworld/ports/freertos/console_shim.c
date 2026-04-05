#include <stdarg.h>
#include "ports/common/platform_port.h"

void console_puts(const char *s) {
    platform_console_write(s);
}

int console_printf(const char *format, ...) {
    va_list args;
    int ret;
    va_start(args, format);
    ret = platform_console_vprintf(format, &args);
    va_end(args);
    return ret;
}
