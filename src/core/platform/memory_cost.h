#ifndef MEMORY_COST_H
#define MEMORY_COST_H

#include <stdint.h>

typedef struct {
    uint32_t flash_bytes;
    uint32_t ram_bytes;
} memory_cost_t;

#endif
