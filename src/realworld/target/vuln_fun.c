#include "realworld/src/led_demo.h"

static __attribute__((used)) int fun1_impl(void) {
    return led_demo_run_unpatched();
}

__attribute__((naked, noinline, used, aligned(4), section(".text.fun1_entry")))
int fun1(void) {
    __asm volatile(
        ".thumb                    \n"
        "b.w   fun1_impl           \n");
}
