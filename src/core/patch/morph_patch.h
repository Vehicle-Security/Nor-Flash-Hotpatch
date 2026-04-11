#ifndef MORPH_PATCH_H
#define MORPH_PATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "../platform/memory_cost.h"

bool morph_patch_apply(void);
void morph_patch_unapply(void);
bool morph_patch_is_active(void);
bool morph_patch_demo_can_run(void);
void morph_patch_print_status(void);
int patch_slot(void);

uintptr_t morph_patch_hardfault_recover(void);
memory_cost_t morph_patch_memory_cost(void);

#endif
