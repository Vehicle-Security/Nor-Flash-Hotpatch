#include "cycle_counter.h"

#include "nrf.h"

static bool g_cycle_counter_ready = false;

bool cycle_counter_init(void) {
#if defined(DWT) && defined(CoreDebug)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

#ifdef DWT_CTRL_NOCYCCNT_Msk
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0u) {
        g_cycle_counter_ready = false;
        return false;
    }
#endif

    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    __DSB();
    __ISB();

    g_cycle_counter_ready = ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u);
    return g_cycle_counter_ready;
#else
    g_cycle_counter_ready = false;
    return false;
#endif
}

bool cycle_counter_reset(void) {
    if (!g_cycle_counter_ready) {
        if (!cycle_counter_init()) {
            return false;
        }
    }

    __DSB();
    __ISB();

    DWT->CYCCNT = 0u;

    __DSB();
    __ISB();

    return true;
}

uint32_t cycle_counter_read(void) {
    if (!g_cycle_counter_ready) {
        if (!cycle_counter_init()) {
            return 0xFFFFFFFFu;
        }
    }

    __DSB();
    __ISB();

    return DWT->CYCCNT;
}
