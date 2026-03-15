#ifndef RAPIDPATCH_VM_H
#define RAPIDPATCH_VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RAPIDPATCH_FILTER_PASS = 0,
    RAPIDPATCH_FILTER_DROP = 1,
    RAPIDPATCH_FILTER_REDIRECT = 2,
    RAPIDPATCH_FIXED_OP_PASS = 0x00010000u,
};

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t lr;
} rapidpatch_fixed_frame_t;

typedef struct {
    const uint8_t *code;
    uint16_t code_len;
} rapidpatch_vm_t;

bool rapidpatch_vm_init(rapidpatch_vm_t *vm, const uint8_t *code, uint16_t code_len);
uint64_t rapidpatch_vm_exec(const rapidpatch_vm_t *vm, void *ctx, size_t ctx_len);

#endif
