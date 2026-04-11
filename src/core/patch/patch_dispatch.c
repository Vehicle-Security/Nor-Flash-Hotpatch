#include "patch_control.h"

#include "morph_patch.h"
#include "ports/common/platform_port.h"

const char *patch_scheme_name(void) {
    return "MorphPatch";
}

int patch_call(void) {
    return platform_patch_call(patch_slot);
}

bool patch_apply(void) {
    bool applied = false;

    platform_victim_suspend();
    platform_lock_patch_region();
    applied = morph_patch_apply();
    platform_unlock_patch_region();
    platform_victim_resume();

    return applied;
}

void patch_unapply(void) {
    platform_victim_suspend();
    platform_lock_patch_region();
    morph_patch_unapply();
    platform_unlock_patch_region();
    platform_victim_resume();
}

bool patch_is_active(void) {
    return morph_patch_is_active();
}

bool patch_demo_can_run(void) {
    return morph_patch_demo_can_run();
}

void print_patch_status(void) {
    morph_patch_print_status();
}

memory_cost_t patch_memory_cost(void) {
    return morph_patch_memory_cost();
}
