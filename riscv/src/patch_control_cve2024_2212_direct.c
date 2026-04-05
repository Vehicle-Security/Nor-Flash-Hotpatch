#include "patch_control.h"

#include <stdint.h>

#include "board_port.h"
#include "cve_target.h"
#include "targets.h"

extern const uint32_t patch_slot_cve2024_2212_direct_word;
extern const uint32_t patch_slot_cve2024_2212_direct_target;

enum {
    PATCH_INSTR_STATE0 = 0x02211063u,
    PATCH_INSTR_STATE1 = 0x02210063u,
    PATCH_INSTR_STATE2 = 0x02200063u,
};

typedef enum {
    PATCH_SLOT_STATE_UNKNOWN = -1,
    PATCH_SLOT_STATE0 = 0,
    PATCH_SLOT_STATE1 = 1,
    PATCH_SLOT_STATE2 = 2,
} patch_slot_state_t;

static uint32_t patch_read_slot_word(void)
{
    return *(const volatile uint32_t *)&patch_slot_cve2024_2212_direct_word;
}

static uintptr_t patch_slot_addr(void)
{
    return (uintptr_t)&patch_slot_cve2024_2212_direct_word;
}

static uintptr_t patch_slot_target_addr(void)
{
    return (uintptr_t)&patch_slot_cve2024_2212_direct_target;
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

static bool patch_target_is_supported(void)
{
    return cve_target_get_current() == CVE_TARGET_CVE2024_2212;
}

static int direct_patch_call(void)
{
    return patch_slot_cve2024_2212_direct();
}

static bool direct_patch_apply(void)
{
    const uint32_t current_word = patch_read_slot_word();

    if (!patch_target_is_supported()) {
        return false;
    }

    if (current_word != PATCH_INSTR_STATE0) {
        return false;
    }

    if (!board_patch_write_word_with_erase(patch_slot_addr(), PATCH_INSTR_STATE0, PATCH_INSTR_STATE1)) {
        return false;
    }

    board_code_sync();
    return patch_read_slot_word() == PATCH_INSTR_STATE1;
}

static void direct_patch_unapply(void)
{
    const uint32_t current_word = patch_read_slot_word();

    if (current_word != PATCH_INSTR_STATE1) {
        return;
    }

    (void)board_patch_write_word_with_erase(patch_slot_addr(), PATCH_INSTR_STATE1, PATCH_INSTR_STATE2);
    board_code_sync();
}

static bool direct_patch_is_active(void)
{
    return patch_read_slot_word() == PATCH_INSTR_STATE1;
}

static bool direct_patch_demo_can_run(void)
{
    return patch_target_is_supported() && (patch_read_slot_word() == PATCH_INSTR_STATE0);
}

static void direct_print_patch_status(void)
{
    const uint32_t slot_word = patch_read_slot_word();
    const uintptr_t slot_addr = patch_slot_addr();
    const uintptr_t target_addr = patch_slot_target_addr();
    const patch_slot_state_t state = patch_decode_state(slot_word);

    board_console_printf("[patch-2212] slot_addr=0x%08lX slot_word=0x%08lX target_word=0x%08lX target_addr=0x%08lX\r\n",
                         (unsigned long)slot_addr,
                         (unsigned long)slot_word,
                         (unsigned long)PATCH_INSTR_STATE1,
                         (unsigned long)target_addr);
    board_console_printf("[patch-2212] state=%s target_ok=%s\r\n",
                         patch_state_name_from_word(slot_word),
                         patch_target_is_supported() ? "yes" : "no");

    if (state == PATCH_SLOT_STATE0) {
        board_console_write("[patch-2212] semantics: branch not taken -> vulnerable entry fun1\r\n");
    } else if (state == PATCH_SLOT_STATE1) {
        board_console_write("[patch-2212] semantics: branch taken -> fixed entry fun2\r\n");
    } else if (state == PATCH_SLOT_STATE2) {
        board_console_write("[patch-2212] semantics: patch disabled by extra clear bit, no 0->1 rewrite\r\n");
    } else {
        board_console_write("[patch-2212] semantics: unrecognized slot word\r\n");
    }
}

static const patch_scheme_ops_t g_cve2024_2212_direct_scheme = {
    .name = "EraseRewritePatch-RV (same slot, page rewrite)",
    .call = direct_patch_call,
    .apply = direct_patch_apply,
    .unapply = direct_patch_unapply,
    .is_active = direct_patch_is_active,
    .demo_can_run = direct_patch_demo_can_run,
    .print_status = direct_print_patch_status,
};

const patch_scheme_ops_t *patch_scheme_cve2024_2212_direct(void)
{
    return &g_cve2024_2212_direct_scheme;
}
