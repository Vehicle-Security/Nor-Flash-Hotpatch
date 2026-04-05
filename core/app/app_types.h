#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>

typedef uint32_t UBaseType_t;

typedef struct {
    UBaseType_t queue_length;
    UBaseType_t item_size;
    uint8_t *p_mem;
    uint32_t alloc_size;
    uint8_t *p_guard;
} txfr_queue_t;

#endif
