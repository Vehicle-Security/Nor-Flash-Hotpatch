#include "realworld/src/led_demo.h"

static __attribute__((used)) int fun2_impl(void) {
    return led_demo_run_patched();
}

__attribute__((naked, noinline, used, section(".hotpatch_page.entry"), aligned(2)))
int fun2(void) {
    __asm volatile(
        ".thumb        \n"
        "b.w fun2_impl  \n");
}
