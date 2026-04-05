#ifndef CLEARBIT_PATCH_H
#define CLEARBIT_PATCH_H

#include <stdbool.h>
#include <stdint.h>

bool clearbit_patch_apply(void);
void clearbit_patch_unapply(void);
bool clearbit_patch_is_active(void);
bool clearbit_patch_demo_can_run(void);
void clearbit_patch_print_status(void);
int patch_slot(void);

uintptr_t clearbit_patch_hardfault_recover(void);

#endif
