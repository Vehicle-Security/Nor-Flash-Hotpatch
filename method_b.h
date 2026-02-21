#ifndef METHOD_B_H
#define METHOD_B_H

#include "base_module.h"

/* Measure B-method (traditional erase+write flash hotfix). */
bool measure_method_B(uint32_t page_addr, uint16_t hw, uint32_t N, uint32_t *out_cycles);

#endif /* METHOD_B_H */
