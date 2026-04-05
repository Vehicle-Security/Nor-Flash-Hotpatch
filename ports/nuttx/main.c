#include <stdbool.h>

#ifdef __NuttX__
#include <unistd.h>
#else
#include "bsp.h"
#include "core/app/clearbit_app.h"
#endif

#include "nuttx_port.h"

#ifdef __NuttX__
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (!nuttx_port_init()) {
        return -1;
    }

    if (!nuttx_port_start()) {
        return -1;
    }

    for (;;) {
        sleep(1);
    }
}
#else
int main(void) {
    bsp_board_init(BSP_INIT_LEDS);

    if (!nuttx_port_init()) {
        return -1;
    }

    if (!nuttx_port_start()) {
        return -1;
    }

    clearbit_app_run_forever();
    return -1;
}
#endif
