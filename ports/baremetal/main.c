#include "bsp.h"
#include "core/app/morph_app.h"

int main(void) {
    bsp_board_init(BSP_INIT_LEDS);
    morph_app_run_forever();
}
