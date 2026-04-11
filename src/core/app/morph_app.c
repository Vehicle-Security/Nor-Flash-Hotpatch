#include "morph_app.h"

#include <stdbool.h>
#include <stddef.h>

#include "../console/console.h"
#include "../platform/cycle_counter.h"

void morph_app_boot(void) {
    console_init();

    if (cycle_counter_init()) {
        console_puts("[init] DWT cycle counter ready.\r\n");
    } else {
        console_puts("[init] Warning: DWT cycle counter unavailable.\r\n");
    }

    console_print_help();
    console_print_status();
    console_run_startup_smoke_test();
    console_prompt();
}

void morph_app_process_once(void) {
    static char cmd[48];
    static size_t len = 0u;

    if (!console_poll_line(cmd, sizeof(cmd), &len)) {
        return;
    }

    console_handle_command(cmd);
    len = 0u;
    console_prompt();
}

void morph_app_run_forever(void) {
    morph_app_boot();

    while (true) {
        morph_app_process_once();
    }
}
