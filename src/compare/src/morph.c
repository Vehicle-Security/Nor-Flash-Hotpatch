/* MorphPatch adapter for the compare framework. */
#include "compare.h"

#include "core/patch/morph_patch.h"
#include "core/target/cve_target.h"

bool morph_patch_install(void) {
    return morph_patch_apply();
}

int morph_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    cve_target_set_benchmark_override_from_regs(r0, r1, r2, r3);
    return patch_slot();
}

memory_cost_t morph_memory_cost(void) {
    return morph_patch_memory_cost();
}
