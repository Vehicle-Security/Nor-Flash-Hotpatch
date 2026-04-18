#ifndef LED_DEMO_H
#define LED_DEMO_H

#include <stdbool.h>

void led_demo_init(void);
int led_demo_run_unpatched(void);
int led_demo_run_patched(void);
bool led_demo_primary_led_is_on(void);
bool led_demo_patch_led_is_on(void);

#endif
