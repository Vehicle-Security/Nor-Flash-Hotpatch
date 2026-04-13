#ifndef MORPH_PATCH_H
#define MORPH_PATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "../platform/memory_cost.h"

typedef enum {
    MORPH_PATCH_PATH_DIRECT = 0,
    MORPH_PATCH_PATH_FAULT = 1,
} morph_patch_path_t;

void morph_patch_set_path(morph_patch_path_t path);
morph_patch_path_t morph_patch_get_path(void);
const char *morph_patch_path_name(void);

bool morph_patch_apply(void);
void morph_patch_unapply(void);
bool morph_patch_is_active(void);
bool morph_patch_demo_can_run(void);
void morph_patch_print_status(void);
bool morph_patch_current_slot_uses_fault_dispatch(void);
void morph_patch_fault_begin(void);
void morph_patch_fault_end(void);
int morph_patch_fault_entry(void);
int patch_slot(void);

uintptr_t morph_patch_fault_recover(uint32_t *stacked_frame);
memory_cost_t morph_patch_memory_cost(void);

#endif
