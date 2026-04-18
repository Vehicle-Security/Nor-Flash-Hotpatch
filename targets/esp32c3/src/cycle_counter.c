#include "cycle_counter.h"
#include "esp_cpu.h"

static uint64_t s_baseline = 0u;

bool cycle_counter_init(void)
{
    return true;
}

bool cycle_counter_reset(void)
{
    s_baseline = (uint64_t)esp_cpu_get_cycle_count();
    return true;
}

uint64_t cycle_counter_read(void)
{
    uint64_t now = (uint64_t)esp_cpu_get_cycle_count();
    return now - s_baseline;
}
