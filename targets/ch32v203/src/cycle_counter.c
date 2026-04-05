#include "cycle_counter.h"

#include "ch32v20x.h"

static uint64_t s_cycle_baseline = 0u;
static bool s_cycle_counter_ready = false;

#define SYSTICK_CTLR_STE   (1u << 0)
#define SYSTICK_CTLR_MODE  (1u << 4)
#define SYSTICK_CTLR_INIT  (1u << 5)
#define SYSTICK_CYCLES_PER_TICK 8u

static uint64_t systick_read_cnt(void)
{
    volatile uint32_t *const cnt32 = (volatile uint32_t *)&(SysTick->CNT);
    uint32_t hi_before;
    uint32_t lo;
    uint32_t hi_after;

    do {
        hi_before = cnt32[1];
        lo = cnt32[0];
        hi_after = cnt32[1];
    } while (hi_before != hi_after);

    return (((uint64_t)hi_before) << 32) | (uint64_t)lo;
}

static uint64_t cycle_counter_read_raw(void)
{
    return systick_read_cnt() * (uint64_t)SYSTICK_CYCLES_PER_TICK;
}

bool cycle_counter_init(void)
{
    uint64_t start;
    uint64_t end;
    volatile uint32_t i;

    SystemCoreClockUpdate();
    SysTick->CTLR = 0u;
    SysTick->SR = 0u;
    SysTick->CNT = 0u;
    SysTick->CMP = 0xFFFFFFFFFFFFFFFFull;
    SysTick->CTLR = SYSTICK_CTLR_INIT | SYSTICK_CTLR_MODE | SYSTICK_CTLR_STE;

    start = cycle_counter_read_raw();
    for (i = 0u; i < 32u; ++i) {
        __NOP();
    }
    end = cycle_counter_read_raw();

    if (end == start) {
        SysTick->CTLR = 0u;
        return false;
    }

    s_cycle_counter_ready = true;
    s_cycle_baseline = end;
    return true;
}

bool cycle_counter_reset(void)
{
    if (!s_cycle_counter_ready) {
        return false;
    }

    s_cycle_baseline = cycle_counter_read_raw();
    return true;
}

uint64_t cycle_counter_read(void)
{
    uint64_t now;

    if (!s_cycle_counter_ready) {
        return 0u;
    }

    now = cycle_counter_read_raw();
    if (now >= s_cycle_baseline) {
        return now - s_cycle_baseline;
    }
    return s_cycle_baseline - now;
}
