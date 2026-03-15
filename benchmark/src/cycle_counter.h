#ifndef CYCLE_COUNTER_H
#define CYCLE_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

#include "patch_control.h"

bool cycle_counter_init(void);
bool cycle_counter_reset(void);
uint32_t cycle_counter_read(void);
uint32_t measure_cycles(void (*fn)(void));
uint32_t measure_patch_apply_cycles(patch_scheme_t scheme);
uint32_t measure_patch_unpatch_cycles(patch_scheme_t scheme);
uint32_t measure_patch_call_cycles(patch_scheme_t scheme);

#endif
