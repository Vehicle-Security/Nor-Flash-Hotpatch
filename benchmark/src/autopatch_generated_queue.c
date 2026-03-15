#include "autopatch_symbols.h"

#include "patch_result.h"

uint64_t autopatch_filter_queue(autopatch_stack_frame_t *frame) {
    uint32_t op = AUTOPATCH_FILTER_PASS;
    int32_t ret_code = PATCH_RESULT_SAFE_NOOP;

    if (frame == 0) {
        op = AUTOPATCH_FILTER_DROP;
        ret_code = -127;
    } else if (frame->r0 == 0u) {
        op = AUTOPATCH_FILTER_DROP;
        ret_code = -1;
    } else if (frame->r1 == 0u) {
        op = AUTOPATCH_FILTER_DROP;
        ret_code = -2;
    } else if (frame->r0 > (0xFFFFFFFFu / frame->r1)) {
        op = AUTOPATCH_FILTER_DROP;
        ret_code = PATCH_RESULT_ATTACK_BLOCKED;
    }

    return ((uint64_t)op << 32) | (uint32_t)ret_code;
}
