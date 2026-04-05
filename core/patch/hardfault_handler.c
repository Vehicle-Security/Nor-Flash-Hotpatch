/*
 * hardfault_handler.c — ClearBitPatch HardFault recovery handler.
 *
 * Overrides the weak default HardFault_Handler from the nRF52 startup file.
 * When a flash-write failure in clearbit_patch_apply() triggers a deliberate
 * HardFault (via UDF #0), this handler retries the write one last time.
 * If the write still fails, the stacked PC is redirected to the fix function
 * (fun2) so execution continues through the patched code path despite the
 * flash programming error.
 */
#include <stdint.h>
#include "clearbit_patch.h"

/*
 * Cortex-M4 exception stack frame (8 words, low to high address):
 *   [0] R0  [1] R1  [2] R2  [3] R3
 *   [4] R12 [5] LR  [6] PC  [7] xPSR
 *
 * Modifying [6] changes where execution resumes after exception return.
 */

void HardFault_Handler_C(uint32_t *stacked_frame) {
    uintptr_t redirect = clearbit_patch_hardfault_recover();

    if (redirect != 0) {
        stacked_frame[6] = (uint32_t)redirect;
        return;
    }

    /* Not a patch-recovery fault — hang for debugger. */
    while (1) {
    }
}

/*
 * Naked entry: determine which stack pointer was active at fault time
 * (EXC_RETURN bit 2: 0 = MSP, 1 = PSP), then tail-call the C handler.
 */
__attribute__((naked))
void HardFault_Handler(void) {
    __asm volatile(
        "tst   lr, #4           \n"
        "ite   eq               \n"
        "mrseq r0, msp          \n"
        "mrsne r0, psp          \n"
        "b     HardFault_Handler_C \n"
    );
}
