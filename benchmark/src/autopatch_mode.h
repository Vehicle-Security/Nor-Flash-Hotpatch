#ifndef AUTOPATCH_MODE_H
#define AUTOPATCH_MODE_H

#include "app_common.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    UBaseType_t queue_length;
    UBaseType_t item_size;
} autopatch_input_t;

const autopatch_input_t *autopatch_default_input(void);
uintptr_t autopatch_filter_addr(void);
bool autopatch_is_ready(void);
bool autopatch_is_enabled(void);
bool autopatch_set_enabled(bool enabled);
bool autopatch_supports_online_toggle(void);
bool autopatch_invoke_filter(UBaseType_t queue_length, UBaseType_t item_size, uint32_t *op, int32_t *ret_code);
int autopatch_patch_slot(void);
int autopatch_background_call(void);
int autopatch_patched_call(void);
void autopatch_print_status(void);

#endif
