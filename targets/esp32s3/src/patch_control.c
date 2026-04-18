/*
 * patch_control.c -- MorphPatch for Xtensa (ESP32-S3).
 *
 * Uses the same monotonic bit-clear strategy as the RISC-V version but operates
 * on 3-byte Xtensa branch instructions masked to 24 bits.
 * Three states are encoded by progressively clearing bits:
 *   STATE0 (0x0004CFF7) -- bnall a15,a15  (not taken when a15=1, falls through to fun1)
 *   STATE1 (0x00044FF7) -- ball  a15,a15  (taken when a15=1, jumps to fun2)
 *   STATE2 (0x00040FF7) -- bnone a15,a15  (not taken when a15=1, falls through to fun1)
 */
#include "patch_control.h"

#include <stdint.h>

#include "board_port.h"
#include "targets.h"

extern const uint8_t patch_slot_begin;

enum {
    PATCH_INSTR_STATE0 = 0x0004CFF7u,  /* bnall a15,a15 -- not taken */
    PATCH_INSTR_STATE1 = 0x00044FF7u,  /* ball  a15,a15 -- taken     */
    PATCH_INSTR_STATE2 = 0x00040FF7u,  /* bnone a15,a15 -- not taken */
};

#define PATCH_SLOT_WORD_MASK 0x00FFFFFFu

typedef enum {
    PATCH_SLOT_STATE_UNKNOWN = -1,
    PATCH_SLOT_STATE0 = 0,
    PATCH_SLOT_STATE1 = 1,
    PATCH_SLOT_STATE2 = 2,
} patch_slot_state_t;

static uintptr_t patch_slot_address(void)
{
    return (uintptr_t)&patch_slot_begin;
}

static uint32_t patch_read_slot_word(void)
{
    return board_patch_read_word(patch_slot_address());
}

static patch_slot_state_t patch_decode_state(uint32_t slot_word)
{
    uint32_t masked = slot_word & PATCH_SLOT_WORD_MASK;

    if (masked == (PATCH_INSTR_STATE0 & PATCH_SLOT_WORD_MASK)) {
        return PATCH_SLOT_STATE0;
    }
    if (masked == (PATCH_INSTR_STATE1 & PATCH_SLOT_WORD_MASK)) {
        return PATCH_SLOT_STATE1;
    }
    if (masked == (PATCH_INSTR_STATE2 & PATCH_SLOT_WORD_MASK)) {
        return PATCH_SLOT_STATE2;
    }
    return PATCH_SLOT_STATE_UNKNOWN;
}

static const char *patch_state_name_from_word(uint32_t slot_word)
{
    switch (patch_decode_state(slot_word)) {
        case PATCH_SLOT_STATE0:
            return "STATE0(pristine)";
        case PATCH_SLOT_STATE1:
            return "STATE1(patched)";
        case PATCH_SLOT_STATE2:
            return "STATE2(unpatched-no-repatch)";
        default:
            return "UNKNOWN";
    }
}

static int default_patch_call(void)
{
    return patch_slot_call();
}

static bool default_patch_apply(void)
{
    const uint32_t current_word = patch_read_slot_word();

    if ((current_word & PATCH_SLOT_WORD_MASK) != (PATCH_INSTR_STATE0 & PATCH_SLOT_WORD_MASK)) {
        return false;
    }

    if (!board_patch_write_word_monotonic(patch_slot_address(), PATCH_INSTR_STATE0, PATCH_INSTR_STATE1)) {
        return false;
    }

    return (patch_read_slot_word() & PATCH_SLOT_WORD_MASK) == (PATCH_INSTR_STATE1 & PATCH_SLOT_WORD_MASK);
}

static void default_patch_unapply(void)
{
    const uint32_t current_word = patch_read_slot_word();

    if ((current_word & PATCH_SLOT_WORD_MASK) != (PATCH_INSTR_STATE1 & PATCH_SLOT_WORD_MASK)) {
        return;
    }

    (void)board_patch_write_word_monotonic(patch_slot_address(), PATCH_INSTR_STATE1, PATCH_INSTR_STATE2);
}

static bool default_patch_is_active(void)
{
    return (patch_read_slot_word() & PATCH_SLOT_WORD_MASK) == (PATCH_INSTR_STATE1 & PATCH_SLOT_WORD_MASK);
}

static bool default_patch_demo_can_run(void)
{
    return (patch_read_slot_word() & PATCH_SLOT_WORD_MASK) == (PATCH_INSTR_STATE0 & PATCH_SLOT_WORD_MASK);
}

static void default_print_patch_status(void)
{
    const uint32_t slot_word = patch_read_slot_word();
    const uintptr_t slot_addr = patch_slot_address();
    const patch_slot_state_t state = patch_decode_state(slot_word);

    board_console_printf("[patch] slot_addr=0x%08lX slot_word=0x%06lX\r\n",
                         (unsigned long)slot_addr,
                         (unsigned long)(slot_word & PATCH_SLOT_WORD_MASK));
    board_console_printf("[patch] state=%s\r\n", patch_state_name_from_word(slot_word));

    if (state == PATCH_SLOT_STATE0) {
        board_console_write("[patch] semantics: branch not taken -> vulnerable entry fun1\r\n");
    } else if (state == PATCH_SLOT_STATE1) {
        board_console_write("[patch] semantics: branch taken -> fixed entry fun2\r\n");
    } else if (state == PATCH_SLOT_STATE2) {
        board_console_write("[patch] semantics: patch disabled by extra clear bit, no 0->1 rewrite\r\n");
    } else {
        board_console_write("[patch] semantics: unrecognized slot word\r\n");
    }
}

static const patch_scheme_ops_t g_default_patch_scheme = {
    .name = "MorphPatch-Xtensa (erase-free, 1->0 only)",
    .call = default_patch_call,
    .apply = default_patch_apply,
    .unapply = default_patch_unapply,
    .is_active = default_patch_is_active,
    .demo_can_run = default_patch_demo_can_run,
    .print_status = default_print_patch_status,
};

const patch_scheme_ops_t *patch_scheme_default(void)
{
    return &g_default_patch_scheme;
}

const char *patch_scheme_name(void)
{
    return g_default_patch_scheme.name;
}

int patch_call(void)
{
    return g_default_patch_scheme.call();
}

bool patch_apply(void)
{
    return g_default_patch_scheme.apply();
}

void patch_unapply(void)
{
    g_default_patch_scheme.unapply();
}

bool patch_is_active(void)
{
    return g_default_patch_scheme.is_active();
}

bool patch_demo_can_run(void)
{
    return g_default_patch_scheme.demo_can_run();
}

void print_patch_status(void)
{
    g_default_patch_scheme.print_status();
}
