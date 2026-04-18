/*
 * patch_control.c -- MorphPatch for RISC-V (ESP32-C3).
 *
 * Uses the same monotonic bit-clear strategy as the CH32V203 version but
 * operates through ESP-IDF partition APIs for flash writes.  Reading is
 * done via the memory-mapped volatile pointer (ESP32-C3 code flash is
 * directly mapped through cache).
 *
 * Three states are encoded by progressively clearing bits in the BEQ opcode:
 *   STATE0 (0x02211063) -- original, branches to fun1 (vulnerable path)
 *   STATE1 (0x02210063) -- patched,  branches to fun2 (fixed path)
 *   STATE2 (0x02200063) -- unpatched, falls through (fun1 path, post-erase)
 */
#include "patch_control.h"

#include <stdint.h>

#include "board_port.h"
#include "targets.h"

extern const uint32_t patch_slot_word;

enum {
    PATCH_INSTR_STATE0 = 0x02211063u,  /* BEQ x2,x2,fun1 (always-taken) */
    PATCH_INSTR_STATE1 = 0x02210063u,  /* BEQ x2,x1,fun2 (patch active) */
    PATCH_INSTR_STATE2 = 0x02200063u,  /* BEQ x0,x0,+0   (fall through) */
};

typedef enum {
    PATCH_SLOT_STATE_UNKNOWN = -1,
    PATCH_SLOT_STATE0 = 0,
    PATCH_SLOT_STATE1 = 1,
    PATCH_SLOT_STATE2 = 2,
} patch_slot_state_t;

static uint32_t patch_read_slot_word(void)
{
    return *(const volatile uint32_t *)&patch_slot_word;
}

static uintptr_t patch_slot_addr(void)
{
    return (uintptr_t)&patch_slot_word;
}

static patch_slot_state_t patch_decode_state(uint32_t slot_word)
{
    switch (slot_word) {
        case PATCH_INSTR_STATE0:
            return PATCH_SLOT_STATE0;
        case PATCH_INSTR_STATE1:
            return PATCH_SLOT_STATE1;
        case PATCH_INSTR_STATE2:
            return PATCH_SLOT_STATE2;
        default:
            return PATCH_SLOT_STATE_UNKNOWN;
    }
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
    return patch_slot();
}

static bool default_patch_apply(void)
{
    const uint32_t current_word = patch_read_slot_word();

    if (current_word != PATCH_INSTR_STATE0) {
        return false;
    }

    if (!board_patch_write_word_monotonic(patch_slot_addr(), PATCH_INSTR_STATE0, PATCH_INSTR_STATE1)) {
        return false;
    }

    return patch_read_slot_word() == PATCH_INSTR_STATE1;
}

static void default_patch_unapply(void)
{
    const uint32_t current_word = patch_read_slot_word();

    if (current_word != PATCH_INSTR_STATE1) {
        return;
    }

    (void)board_patch_write_word_monotonic(patch_slot_addr(), PATCH_INSTR_STATE1, PATCH_INSTR_STATE2);
}

static bool default_patch_is_active(void)
{
    return patch_read_slot_word() == PATCH_INSTR_STATE1;
}

static bool default_patch_demo_can_run(void)
{
    return patch_read_slot_word() == PATCH_INSTR_STATE0;
}

static void default_print_patch_status(void)
{
    const uint32_t slot_word = patch_read_slot_word();
    const uintptr_t slot_addr = patch_slot_addr();
    const patch_slot_state_t state = patch_decode_state(slot_word);

    board_console_printf("[patch] slot_addr=0x%08lX slot_word=0x%08lX\r\n",
                         (unsigned long)slot_addr,
                         (unsigned long)slot_word);
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
    .name = "MorphPatch-RV (erase-free, 1->0 only)",
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
