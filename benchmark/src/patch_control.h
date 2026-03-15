#ifndef PATCH_CONTROL_H
#define PATCH_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PATCH_SCHEME_LEGACY = 0,
    PATCH_SCHEME_RAPID = 1,
    PATCH_SCHEME_HERA = 2,
    PATCH_SCHEME_AUTOPATCH = 3,
} patch_scheme_t;

int patch_slot(void);
int rapid_patch_slot(void);

const char *patch_scheme_name(patch_scheme_t scheme);
int patch_call(patch_scheme_t scheme);
bool patch_apply(patch_scheme_t scheme);
void patch_unapply(patch_scheme_t scheme);
bool patch_is_active(patch_scheme_t scheme);
bool patch_demo_can_run(patch_scheme_t scheme);
bool patch_supports_online_toggle(patch_scheme_t scheme);
void print_patch_status(patch_scheme_t scheme);
void print_all_patch_status(void);

uintptr_t patch_slot_addr(void);
uint16_t read_patch_halfword(void);
int rapid_fixed_patch_point_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);
uint32_t rapid_patch_install_addr(void);
uint16_t rapid_patch_code_size(void);
const uint8_t *rapid_patch_code_bytes(void);
uintptr_t hera_patch_payload_addr(void);

#endif
