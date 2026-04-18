/*
 * cycle_counter.c -- ESP32-S3 (Xtensa) cycle counter using CCOUNT register.
 */
#include "cycle_counter.h"

#include <stdint.h>
#include "xtensa/core-macros.h"

static uint32_t s_baseline = 0u;

bool cycle_counter_init(void)
{
    return true;
}

bool cycle_counter_reset(void)
{
    s_baseline = XTHAL_GET_CCOUNT();
    return true;
}

uint64_t cycle_counter_read(void)
{
    uint32_t now = XTHAL_GET_CCOUNT();
    /* Handle 32-bit wrap-around */
    return (uint64_t)(now - s_baseline);
}
