#include "patch_result.h"

bool patch_result_is_fixed(int ret_code) {
    return ret_code == -1
        || ret_code == -2
        || ret_code == PATCH_RESULT_ATTACK_BLOCKED
        || ret_code == PATCH_RESULT_SAFE_EXECUTED;
}

bool patch_result_is_vulnerable(int ret_code) {
    return ret_code == PATCH_RESULT_ATTACK_OVERFLOW;
}

const char *patch_result_name(int ret_code) {
    if (ret_code == PATCH_RESULT_ATTACK_BLOCKED) {
        return "blocked";
    }
    if (ret_code == PATCH_RESULT_ATTACK_OVERFLOW) {
        return "overflow";
    }
    if (ret_code == PATCH_RESULT_SAFE_EXECUTED) {
        return "safe";
    }
    if (ret_code == PATCH_RESULT_SAFE_NOOP) {
        return "noop";
    }
    if (ret_code == -1) {
        return "reject:length";
    }
    if (ret_code == -2) {
        return "reject:item";
    }
    return "error";
}
