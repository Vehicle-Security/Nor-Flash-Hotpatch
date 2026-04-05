#include "led_demo.h"

#include <stdbool.h>

#include "bsp.h"

enum {
    LED_DEMO_PRIMARY_LED = BSP_BOARD_LED_0,
    LED_DEMO_PATCH_LED = BSP_BOARD_LED_1,
};

static bool g_primary_led_on = false;

static int led_demo_step(bool patch_active) {
    g_primary_led_on = !g_primary_led_on;

    if (g_primary_led_on) {
        bsp_board_led_on(LED_DEMO_PRIMARY_LED);
    } else {
        bsp_board_led_off(LED_DEMO_PRIMARY_LED);
    }

    if (patch_active) {
        bsp_board_led_on(LED_DEMO_PATCH_LED);
    } else {
        bsp_board_led_off(LED_DEMO_PATCH_LED);
    }

    return patch_active ? 1 : 0;
}

void led_demo_init(void) {
    g_primary_led_on = false;
    bsp_board_led_off(LED_DEMO_PRIMARY_LED);
    bsp_board_led_off(LED_DEMO_PATCH_LED);
    bsp_board_led_off(BSP_BOARD_LED_2);
    bsp_board_led_off(BSP_BOARD_LED_3);
}

int led_demo_run_unpatched(void) {
    return led_demo_step(false);
}

int led_demo_run_patched(void) {
    return led_demo_step(true);
}
