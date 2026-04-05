#include "ebpf_helper.h"

uint64_t filter(stack_frame *frame) {
    uint64_t uxQueueLength = (uint64_t)frame->r0;
    uint64_t uxItemSize = (uint64_t)frame->r1;
    uint64_t op = FILTER_PASS;
    uint64_t ret_code = 0;

    if (uxQueueLength == 0ull) {
        op = FILTER_DROP;
        ret_code = (uint64_t)-1;
    } else if (uxItemSize == 0ull) {
        op = FILTER_DROP;
        ret_code = (uint64_t)-2;
    } else if (uxQueueLength > (0xFFFFFFFFull / uxItemSize)) {
        op = FILTER_DROP;
        ret_code = (uint64_t)-3;
    }

    return set_return(op, ret_code);
}
