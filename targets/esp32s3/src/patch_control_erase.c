/*
 * patch_control_erase.c -- Erase-rewrite patch scheme for Xtensa (ESP32-S3).
 *
 * The erase slot starts with a NOP (or a15,a15,a15 = 0x20FFF0, masked 0x00FFF020).
 * After erase+rewrite to STATE1 (ball a15,a15 = 0x00044FF7), the branch is taken
 * and jumps to fun2.  The slot can then be erased+rewritten to STATE2 to un-patch.
 */
#include "patch_control.h"

#include <stdint.h>

#include "board_port.h"
#include "targets.h"

extern const uint8_t patch_slot_erase_begin;
extern const uint8_t patch_slot_erase_sector_begin;

enum {
    PATCH_INSTR_STATE0 = 0x0004CFF7u,  /* bnall a15,a15 */
    PATCH_INSTR_STATE1 = 0x00044FF7u,  /* ball  a15,a15 */
    PATCH_INSTR_STATE2 = 0x00040FF7u,  /* bnone a15,a15 */
};

#define PATCH_SLOT_WORD_MASK 0x00FFFFFFu

/*
 * The NOP encoding for `or a15, a15, a15` in Xtensa is 0x20FFF0.
 * Masked to 24 bits: 0x20FFF0.  This is what the erase slot starts as.
 */
#define ERASE_SLOT_NOP_WORD 0x0020FFF0u

typedef enum {
    PATCH_SLOT_STATE_UNKNOWN = -1,
    PATCH_SLOT_STATE0 = 0,
    PATCH_SLOT_STATE1 = 1,
    PATCH_SLOT_STATE2 = 2,
} patch_slot_state_t;

static uintptr_t patch_slot_address(void)
{
    return (uintptr_t)&patch_slot_erase_begin;
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

static int erase_patch_call(void)
{
    return patch_slot_erase_call();
}

static bool erase_patch_apply(void)
{
    const uint32_t current_word = patch_read_slot_word();
    const uint32_t masked = current_word & PATCH_SLOT_WORD_MASK;

    /*
     * The erase slot starts as NOP (or a15,a15,a15).
     * We can only apply if it is in the initial NOP state
     * (not already STATE1 or any other recognized state).
     */
    if (masked == (PATCH_INSTR_STATE1 & PATCH_SLOT_WORD_MASK)) {
        return false;  /* already patched */
    }

    if (!board_patch_write_word_erase_rewrite(patch_slot_address(), current_word, PATCH_INSTR_STATE1)) {
        return false;
    }

    board_code_sync();
    return (patch_read_slot_word() & PATCH_SLOT_WORD_MASK) == (PATCH_INSTR_STATE1 & PATCH_SLOT_WORD_MASK);
}

static void erase_patch_unapply(void)
{
    const uint32_t current_word = patch_read_slot_word();

    if ((current_word & PATCH_SLOT_WORD_MASK) != (PATCH_INSTR_STATE1 & PATCH_SLOT_WORD_MASK)) {
        return;
    }

    (void)board_patch_write_word_erase_rewrite(patch_slot_address(), PATCH_INSTR_STATE1, PATCH_INSTR_STATE2);
    board_code_sync();
}

static bool erase_patch_is_active(void)
{
    return (patch_read_slot_word() & PATCH_SLOT_WORD_MASK) == (PATCH_INSTR_STATE1 & PATCH_SLOT_WORD_MASK);
}

static bool erase_patch_demo_can_run(void)
{
    /*
     * The erase slot can run the demo as long as it is NOT already in STATE1.
     * The initial NOP state or any other non-STATE1 state is acceptable because
     * erase-rewrite can set it to STATE1.
     */
    return (patch_read_slot_word() & PATCH_SLOT_WORD_MASK) != (PATCH_INSTR_STATE1 & PATCH_SLOT_WORD_MASK);
}

static void erase_print_patch_status(void)
{
    const uint32_t slot_word = patch_read_slot_word();
    const uintptr_t slot_addr = patch_slot_address();
    const patch_slot_state_t state = patch_decode_state(slot_word);

    board_console_printf("[patch-erase] slot_addr=0x%08lX slot_word=0x%06lX\r\n",
                         (unsigned long)slot_addr,
                         (unsigned long)(slot_word & PATCH_SLOT_WORD_MASK));
    board_console_printf("[patch-erase] state=%s\r\n",
                         patch_state_name_from_word(slot_word));

    if (state == PATCH_SLOT_STATE0) {
        board_console_write("[patch-erase] semantics: branch not taken -> vulnerable entry fun1\r\n");
    } else if (state == PATCH_SLOT_STATE1) {
        board_console_write("[patch-erase] semantics: branch taken -> fixed entry fun2\r\n");
    } else if (state == PATCH_SLOT_STATE2) {
        board_console_write("[patch-erase] semantics: patch disabled, erase-rewrite reverted\r\n");
    } else {
        board_console_write("[patch-erase] semantics: NOP or unrecognized (initial state)\r\n");
    }
}

static const patch_scheme_ops_t g_erase_patch_scheme = {
    .name = "EraseRewritePatch-Xtensa (sector erase + rewrite)",
    .call = erase_patch_call,
    .apply = erase_patch_apply,
    .unapply = erase_patch_unapply,
    .is_active = erase_patch_is_active,
    .demo_can_run = erase_patch_demo_can_run,
    .print_status = erase_print_patch_status,
};

const patch_scheme_ops_t *patch_scheme_erase(void)
{
    return &g_erase_patch_scheme;
}
