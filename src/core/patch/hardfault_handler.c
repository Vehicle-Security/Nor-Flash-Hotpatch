/*
 * hardfault_handler.c - MorphPatch fault recovery handlers.
 *
 * The direct MorphPatch path still uses the original inline branch trigger. The
 * optional fault-dispatch path uses 0x4700 ("bx r0") at the patch slot. Its
 * dedicated wrapper saves the caller's original R0 in R12, forces R0 to 0,
 * and the handler restores stacked R0 from stacked R12 before redirecting
 * execution to fun2. That prevents the fault trigger plumbing from corrupting
 * later code.
 *
 */
#include <stdint.h>

#include "nrf.h"

#include "morph_patch.h"

#define MORPH_PATCH_XPSR_T_BIT (1u << 24)

/*
 * Cortex-M4 exception stack frame (8 words, low to high address):
 *   [0] R0  [1] R1  [2] R2  [3] R3
 *   [4] R12 [5] LR  [6] PC  [7] xPSR
 *
 * Modifying [6] changes where execution resumes after exception return.
 */

static void morph_patch_clear_fault_status(void) {
    SCB->CFSR = SCB->CFSR;
    SCB->HFSR = SCB->HFSR;
    SCB->DFSR = SCB->DFSR;
    __DSB();
    __ISB();
}

void MorphPatch_Fault_Handler_C(uint32_t *stacked_frame) {
    uintptr_t redirect = morph_patch_fault_recover(stacked_frame);

    if (redirect != 0) {
        morph_patch_clear_fault_status();
        /*
         * A deliberate "bx r0" fault stacks xPSR with T=0. Restore Thumb state
         * before exception return, otherwise the redirected resume PC would
         * immediately fault again with INVSTATE.
         */
        stacked_frame[6] = (uint32_t)(redirect & ~(uintptr_t)1u);
        stacked_frame[7] |= MORPH_PATCH_XPSR_T_BIT;
        return;
    }

    /* Not a MorphPatch-managed fault; hang for debugger. */
    while (1) {
    }
}

/*
 * Naked entry: determine which stack pointer was active at fault time
 * (EXC_RETURN bit 2: 0 = MSP, 1 = PSP), then tail-call the C handler.
 */
#define MORPH_PATCH_DEFINE_FAULT_WRAPPER(name) \
    __attribute__((naked)) void name(void) { \
        __asm volatile( \
            "tst   lr, #4                     \n" \
            "ite   eq                         \n" \
            "mrseq r0, msp                    \n" \
            "mrsne r0, psp                    \n" \
            "b.w   MorphPatch_Fault_Handler_C \n" \
        ); \
    }

MORPH_PATCH_DEFINE_FAULT_WRAPPER(HardFault_Handler)
MORPH_PATCH_DEFINE_FAULT_WRAPPER(MemoryManagement_Handler)
MORPH_PATCH_DEFINE_FAULT_WRAPPER(BusFault_Handler)
MORPH_PATCH_DEFINE_FAULT_WRAPPER(UsageFault_Handler)
