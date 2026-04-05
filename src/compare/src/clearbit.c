/* ClearBitPatch adapter for the compare framework. */
#include "compare.h"

#include "core/patch/clearbit_patch.h"

bool clearbit_patch_install(void) {
    return clearbit_patch_apply();
}

int clearbit_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    (void)r0;
    (void)r1;
    (void)r2;
    (void)r3;
    return patch_slot();
}
