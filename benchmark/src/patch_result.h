#ifndef PATCH_RESULT_H
#define PATCH_RESULT_H

#include <stdbool.h>

typedef enum {
    PATCH_RESULT_SAFE_NOOP = 0,
    PATCH_RESULT_SAFE_EXECUTED = 1,
    PATCH_RESULT_ATTACK_BLOCKED = -3,
    PATCH_RESULT_ATTACK_OVERFLOW = -30,
} patch_result_code_t;

bool patch_result_is_fixed(int ret_code);
bool patch_result_is_vulnerable(int ret_code);
const char *patch_result_name(int ret_code);

#endif
