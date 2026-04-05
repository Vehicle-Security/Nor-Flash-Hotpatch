#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bsp.h"

#include "freertos_port.h"

int main(void) {
    bsp_board_init(BSP_INIT_LEDS | BSP_INIT_BUTTONS);
    bsp_board_leds_off();

    if (!freertos_port_init()) {
        return -1;
    }

    if (!freertos_port_start()) {
        return -1;
    }

    vTaskStartScheduler();

    return -1;
}
