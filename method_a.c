#include "method_a.h"
#include <stddef.h>
#include "method_a.h"
#define VTOR_ENTRIES (16u + 48u)

typedef struct {
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
} ExceptionStackFrame_t;

__attribute__((aligned(4)))   static uint16_t g_thunk_code[2];
__attribute__((aligned(256))) static uint32_t g_vtor_ram[VTOR_ENTRIES];

static volatile bool            g_patch_enabled = false;
static volatile MethodAStatus_t g_last_status   = HOTFIX_STATUS_NONE;
static volatile uint32_t        g_fault_count   = 0;
static volatile uint32_t        g_slot_addr     = 0;

uint32_t method_a_get_fault_count(void) { return g_fault_count; }
MethodAStatus_t method_a_get_status(void) { return g_last_status; }
void method_a_reset_status(void) { g_last_status = HOTFIX_STATUS_NONE; }
void method_a_disable_patch(void) { g_patch_enabled = false; }

static inline void thunk_set_hw(uint16_t hw) {
    g_thunk_code[0] = hw;
    g_thunk_code[1] = (uint16_t)BXLR_HW;
    memory_barrier();
}

/* Clear sticky fault status bits. */
static inline void clear_fault_status(void) {
    SCB->CFSR = SCB->CFSR;
    SCB->HFSR = SCB->HFSR;
    memory_barrier();
}

/* Dispatch for both HardFault and UsageFault. */
void fault_dispatch(ExceptionStackFrame_t *stacked) {
    g_fault_count++;

    uint32_t faulting_pc = stacked->pc & ~PC_THUMB_BIT;
    uint32_t slot        = (uint32_t)g_slot_addr;

    if (slot != 0u && (faulting_pc == slot || faulting_pc == (slot + 2u))) {
        if (g_patch_enabled) {
            g_last_status = HOTFIX_STATUS_PATCH_OK;
            stacked->pc = ((uint32_t)g_thunk_code) | PC_THUMB_BIT;
        } else {
            g_last_status = HOTFIX_STATUS_INVALID_FAULT;
            /* Skip the invalid halfword, execute the trailing 'bx lr' */
            stacked->pc = (slot + 2u) | PC_THUMB_BIT;
        }
        clear_fault_status();
        return;
    }

    /* Unexpected fault: best-effort "return to caller" via stacked LR. */
    g_last_status = HOTFIX_STATUS_INVALID_FAULT;
    clear_fault_status();
    stacked->pc = (stacked->lr | PC_THUMB_BIT);
}

__attribute__((naked, used)) static void MyHardFault(void) {
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "b fault_dispatch  \n"
    );
}

__attribute__((naked, used)) static void MyUsageFault(void) {
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "b fault_dispatch  \n"
    );
}

void method_a_init(uint32_t slot_page_addr) {
    g_slot_addr     = slot_page_addr;
    g_fault_count   = 0;
    g_last_status   = HOTFIX_STATUS_NONE;
    g_patch_enabled = false;

    uint32_t *vtor_flash = (uint32_t *)SCB->VTOR;
    if (vtor_flash == 0) vtor_flash = (uint32_t *)0x00000000u;

    for (uint32_t i = 0; i < VTOR_ENTRIES; i++) g_vtor_ram[i] = vtor_flash[i];

    /* Override vectors; force Thumb bit explicitly. */
    g_vtor_ram[3] = ((uint32_t)&MyHardFault) | PC_THUMB_BIT;  /* HardFault */
    g_vtor_ram[6] = ((uint32_t)&MyUsageFault) | PC_THUMB_BIT; /* UsageFault */

    SCB->VTOR = (uint32_t)g_vtor_ram;
    memory_barrier();

    /* Enable UsageFault (UDF should raise UsageFault when enabled). */
    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk;
    memory_barrier();

    /* Initialize thunk to a benign instruction */
    thunk_set_hw(0x0000u); /* movs r0, r0 (treated as NOP in Thumb-1) */
}

/* Ensure: with patch disabled, executing the slot triggers exactly one "invalid fault". */
static bool verify_invalid_once(uint32_t page_addr) {
    uint32_t f0 = g_fault_count;
    method_a_disable_patch();
    method_a_reset_status();

    (void)execute_target_slot(page_addr);

    return (g_last_status == HOTFIX_STATUS_INVALID_FAULT) && (g_fault_count == (f0 + 1u));
}

bool measure_method_A(uint32_t page_addr, uint16_t hw, uint32_t N, uint32_t *out_cycles) {
    if (out_cycles == NULL) return false;
    if (!verify_invalid_once(page_addr)) return false;

    uint32_t t0 = cyc_now();

    thunk_set_hw(hw);
    g_patch_enabled = true;
    memory_barrier();

    uint32_t f0 = g_fault_count;

    for (uint32_t i = 0; i < N; i++) {
        method_a_reset_status();
        (void)execute_target_slot(page_addr);
    }

    uint32_t f1 = g_fault_count;

    g_patch_enabled = false;
    thunk_set_hw(0x0000u);
    memory_barrier();

    *out_cycles = (cyc_now() - t0);

    /* Each iteration must fault exactly once at the slot (i.e., redirected to thunk). */
    if ((f1 - f0) != N) return false;
    if (N > 0u && g_last_status != HOTFIX_STATUS_PATCH_OK) return false;

    if (!verify_invalid_once(page_addr)) return false;

    return true;
}
