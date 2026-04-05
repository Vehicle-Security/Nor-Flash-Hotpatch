#include <stdbool.h>

#ifdef CLEARBIT_LITEOS_NATIVE
#include "los_task.h"
#include "los_tick.h"
#else
#include "bsp.h"
#include "core/app/clearbit_app.h"
#endif

#include "liteos_port.h"

#ifdef CLEARBIT_LITEOS_NATIVE
static UINT32 delay_ticks_1s(void) {
    UINT32 ticks = LOS_MS2Tick(1000u);

    return (ticks == 0u) ? 1u : ticks;
}

int main(void) {
    if (!liteos_port_init()) {
        return -1;
    }

    if (!liteos_port_start()) {
        return -1;
    }

    for (;;) {
        (void)LOS_TaskDelay(delay_ticks_1s());
    }
}
#else
int main(void) {
    bsp_board_init(BSP_INIT_LEDS);

    if (!liteos_port_init()) {
        return -1;
    }

    if (!liteos_port_start()) {
        return -1;
    }

    clearbit_app_run_forever();
    return -1;
}
#endif
