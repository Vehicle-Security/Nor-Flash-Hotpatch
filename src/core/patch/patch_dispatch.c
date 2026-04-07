#include "patch_control.h"

#include "clearbit_patch.h"
#include "ports/common/platform_port.h"

const char *patch_scheme_name(void) {
    return "ClearBitPatch";
}

int patch_call(void) {
    return platform_patch_call(patch_slot);
}

bool patch_apply(void) {
    bool applied = false;

    platform_victim_suspend();
    platform_lock_patch_region();
    applied = clearbit_patch_apply();
    platform_unlock_patch_region();
    platform_victim_resume();

    return applied;
}

void patch_unapply(void) {
    platform_victim_suspend();
    platform_lock_patch_region();
    clearbit_patch_unapply();
    platform_unlock_patch_region();
    platform_victim_resume();
}

bool patch_is_active(void) {
    return clearbit_patch_is_active();
}

bool patch_demo_can_run(void) {
    return clearbit_patch_demo_can_run();
}

void print_patch_status(void) {
    clearbit_patch_print_status();
}

memory_cost_t patch_memory_cost(void) {
    return clearbit_patch_memory_cost();
}
