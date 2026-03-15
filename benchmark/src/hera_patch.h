#ifndef HERA_PATCH_H
#define HERA_PATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "app_common.h"

bool hera_patch_install(void);
void hera_patch_unapply(void);
bool hera_patch_is_active(void);
void hera_patch_print_status(void);

#endif
