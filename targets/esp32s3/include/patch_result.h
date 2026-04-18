#ifndef PATCH_RESULT_H
#define PATCH_RESULT_H

#include <stdbool.h>

typedef enum {
    PATCH_RESULT_SAFE_NOOP = 0,
    PATCH_RESULT_SAFE_EXECUTED = 1,
    PATCH_RESULT_ATTACK_BLOCKED = -3,
    PATCH_RESULT_REJECT_DNS_BOUNDS = -4,
    PATCH_RESULT_REJECT_ICMP_FAMILY = -5,
    PATCH_RESULT_ATTACK_OVERFLOW = -30,
    PATCH_RESULT_ATTACK_DNS_OOB_READ = -31,
    PATCH_RESULT_ATTACK_ICMP_MISDISPATCH = -32,
} patch_result_code_t;

bool patch_result_is_fixed(int ret_code);
bool patch_result_is_vulnerable(int ret_code);
const char *patch_result_name(int ret_code);

#endif
