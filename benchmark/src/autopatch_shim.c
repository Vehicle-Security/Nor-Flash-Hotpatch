#include "autopatch_mode.h"
#include "autopatch_symbols.h"

#include "nrf.h"

static volatile bool g_autopatch_enabled = false;
static autopatch_input_t g_autopatch_default_input = {0};

const autopatch_input_t *autopatch_default_input(void) {
    app_get_attack_inputs(&g_autopatch_default_input.queue_length, &g_autopatch_default_input.item_size);
    return &g_autopatch_default_input;
}

uintptr_t autopatch_filter_addr(void) {
    return ((uintptr_t)autopatch_filter_queue) & ~(uintptr_t)1u;
}

bool autopatch_is_ready(void) {
    return true;
}

bool autopatch_is_enabled(void) {
    return autopatch_is_ready() && g_autopatch_enabled;
}

bool autopatch_set_enabled(bool enabled) {
    if (!autopatch_is_ready()) {
        return false;
    }

    g_autopatch_enabled = enabled;
    __DSB();
    __ISB();
    return true;
}

bool autopatch_supports_online_toggle(void) {
    return true;
}

bool autopatch_invoke_filter(UBaseType_t queue_length, UBaseType_t item_size, uint32_t *op, int32_t *ret_code) {
    autopatch_stack_frame_t frame = {0};
    uint64_t raw_ret = 0;

    frame.r0 = (uint32_t)queue_length;
    frame.r1 = (uint32_t)item_size;

    raw_ret = autopatch_filter_queue(&frame);

    if (op != NULL) {
        *op = (uint32_t)(raw_ret >> 32);
    }

    if (ret_code != NULL) {
        *ret_code = (int32_t)(uint32_t)raw_ret;
    }

    return true;
}
