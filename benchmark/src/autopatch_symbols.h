#ifndef AUTOPATCH_SYMBOLS_H
#define AUTOPATCH_SYMBOLS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} autopatch_stack_frame_t;

#define AUTOPATCH_FILTER_PASS     0u
#define AUTOPATCH_FILTER_DROP     1u
#define AUTOPATCH_FILTER_REDIRECT 2u

extern uint64_t autopatch_filter_queue(autopatch_stack_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif
