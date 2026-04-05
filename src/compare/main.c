#include <stddef.h>

#include "bsp.h"

#include "core/console/console.h"
#include "core/platform/cycle_counter.h"

int main(void) {
    char cmd[48];
    size_t len = 0u;

    bsp_board_init(BSP_INIT_LEDS);
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

    while (true) {
        if (!console_poll_line(cmd, sizeof(cmd), &len)) {
            continue;
        }

        console_handle_command(cmd);
        len = 0u;
        console_prompt();
    }
}
