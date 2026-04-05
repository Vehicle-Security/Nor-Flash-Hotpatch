#ifndef PATCH_CONTROL_H
#define PATCH_CONTROL_H

#include <stdbool.h>

typedef struct {
    const char *name;
    int (*call)(void);
    bool (*apply)(void);
    void (*unapply)(void);
    bool (*is_active)(void);
    bool (*demo_can_run)(void);
    void (*print_status)(void);
} patch_scheme_ops_t;

const patch_scheme_ops_t *patch_scheme_default(void);
const patch_scheme_ops_t *patch_scheme_cve2024_2212_direct(void);

const char *patch_scheme_name(void);
int patch_call(void);
bool patch_apply(void);
void patch_unapply(void);
bool patch_is_active(void);
bool patch_demo_can_run(void);
void print_patch_status(void);

#endif
