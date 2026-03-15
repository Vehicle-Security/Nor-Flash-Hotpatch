#ifndef QUEUE_DEMO_H
#define QUEUE_DEMO_H

#include <stdbool.h>

#include "app_common.h"

typedef struct {
    const char *banner;
    const char *status_line;
    const char *reject_prefix;
    const char *reject_wrap_line;
    const char *reject_abort_line;
    const char *done_line;
    bool validate_before_alloc;
} queue_demo_profile_t;

int queue_demo_run(UBaseType_t uxQueueLength,
                   UBaseType_t uxItemSize,
                   bool verbose,
                   const queue_demo_profile_t *profile);

#endif
