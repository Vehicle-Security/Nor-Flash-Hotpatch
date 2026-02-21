#include "method_b.h"
#include "method_a.h"

/*
 * B-method assumes:
 *  - slot starts as UDF (BASE_UDF_HW)
 *  - patch interception is installed (so we can detect faults)
 *  - patch is disabled during B-method execution (we expect 0 faults in the N-loop)
 */
bool measure_method_B(uint32_t page_addr, uint16_t hw, uint32_t N, uint32_t *out_cycles) {
    if (out_cycles == NULL) return false;

    method_a_disable_patch();

    /* Pre-check: slot must fault once when UDF. */
    uint32_t pre_f0 = method_a_get_fault_count();
    method_a_reset_status();
    (void)execute_target_slot(page_addr);
    if (method_a_get_status() != HOTFIX_STATUS_INVALID_FAULT || method_a_get_fault_count() != (pre_f0 + 1u)) {
        return false;
    }

    uint32_t t0 = cyc_now();

    /* 1) Erase+program the target halfword */
    if (!slot_set_hw_with_erase(page_addr, hw)) return false;

    /* 2) Execute N times: must not fault */
    uint32_t f0 = method_a_get_fault_count();
    for (uint32_t i = 0; i < N; i++) {
        method_a_reset_status();
        (void)execute_target_slot(page_addr);
    }
    uint32_t f1 = method_a_get_fault_count();

    /* 3) Restore to UDF (erase+program) */
    if (!slot_set_udf_with_erase(page_addr)) return false;

    *out_cycles = (cyc_now() - t0);

    /* Must not fault during the N-loop */
    if ((f1 - f0) != 0u) return false;

    return true;
}
