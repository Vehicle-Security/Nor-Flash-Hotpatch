#ifndef METHOD_A_H
#define METHOD_A_H

#include "base_module.h"

/* A-method (exception-driven dynamic hotfix) status */
typedef enum {
    HOTFIX_STATUS_NONE = 0,
    HOTFIX_STATUS_INVALID_FAULT = 1,
    HOTFIX_STATUS_PATCH_OK = 2
} MethodAStatus_t;

/* Install HardFault/UsageFault interception and bind the target slot address. */
void method_a_init(uint32_t slot_page_addr);

/* Status/query helpers */
uint32_t        method_a_get_fault_count(void);
MethodAStatus_t method_a_get_status(void);
void            method_a_reset_status(void);
void            method_a_disable_patch(void);

/* Measure A-method: returns success; cycles in out_cycles */
bool measure_method_A(uint32_t page_addr, uint16_t hw, uint32_t N, uint32_t *out_cycles);

#endif /* METHOD_A_H */
