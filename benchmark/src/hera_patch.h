#ifndef HERA_PATCH_H
#define HERA_PATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "app_common.h"

int hera_patch_slot(void);
bool hera_patch_install(void);
void hera_patch_unapply(void);
bool hera_patch_is_active(void);
uintptr_t hera_patch_payload_addr(void);

#endif
