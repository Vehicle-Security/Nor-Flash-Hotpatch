#include "bsp.h"
#include "core/app/clearbit_app.h"

int main(void) {
    bsp_board_init(BSP_INIT_LEDS);
    clearbit_app_run_forever();
}
