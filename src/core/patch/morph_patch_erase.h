#ifndef MORPH_PATCH_ERASE_H
#define MORPH_PATCH_ERASE_H

#include <stdbool.h>

int  erase_patch_call(void);
bool erase_patch_apply(void);
void erase_patch_unapply(void);
bool erase_patch_is_active(void);
bool erase_patch_demo_can_run(void);
void erase_patch_print_status(void);

#endif
