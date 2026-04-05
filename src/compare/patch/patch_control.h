#ifndef PATCH_CONTROL_H
#define PATCH_CONTROL_H

#include <stdbool.h>
typedef enum {
    PATCH_SCHEME_LEGACY = 0,
    PATCH_SCHEME_RAPID = 1,
    PATCH_SCHEME_HERA = 2,
    PATCH_SCHEME_AUTOPATCH = 3,
} patch_scheme_t;

const char *patch_scheme_name(patch_scheme_t scheme);
int patch_call(patch_scheme_t scheme);
bool patch_apply(patch_scheme_t scheme);
void patch_unapply(patch_scheme_t scheme);
bool patch_is_active(patch_scheme_t scheme);
bool patch_demo_can_run(patch_scheme_t scheme);
bool patch_supports_online_toggle(patch_scheme_t scheme);
void print_patch_status(patch_scheme_t scheme);
void print_all_patch_status(void);

#endif
