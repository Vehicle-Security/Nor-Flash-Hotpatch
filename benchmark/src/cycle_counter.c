#include "cycle_counter.h"
#include "patch_control.h"

#include "nrf.h"

static bool g_cycle_counter_ready = false;
static patch_scheme_t g_measure_scheme = PATCH_SCHEME_LEGACY;

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

uint32_t measure_cycles(void (*fn)(void)) {
    if (!cycle_counter_reset()) {
        return 0xFFFFFFFFu;
    }

    fn();

    __DSB();
    __ISB();

    return cycle_counter_read();
}

static void apply_wrapper(void) {
    (void)patch_apply(g_measure_scheme);
}

static void unpatch_wrapper(void) {
    patch_unapply(g_measure_scheme);
}

static void call_wrapper(void) {
    patch_call(g_measure_scheme);
}

uint32_t measure_patch_apply_cycles(patch_scheme_t scheme) {
    g_measure_scheme = scheme;
    return measure_cycles(apply_wrapper);
}

uint32_t measure_patch_unpatch_cycles(patch_scheme_t scheme) {
    g_measure_scheme = scheme;
    return measure_cycles(unpatch_wrapper);
}

uint32_t measure_patch_call_cycles(patch_scheme_t scheme) {
    g_measure_scheme = scheme;
    return measure_cycles(call_wrapper);
}
