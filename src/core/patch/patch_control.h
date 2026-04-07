#ifndef PATCH_CONTROL_H
#define PATCH_CONTROL_H

#include <stdbool.h>

#include "../platform/memory_cost.h"

const char *patch_scheme_name(void);
int patch_call(void);
bool patch_apply(void);
void patch_unapply(void);
bool patch_is_active(void);
bool patch_demo_can_run(void);
void print_patch_status(void);
memory_cost_t patch_memory_cost(void);

#endif
