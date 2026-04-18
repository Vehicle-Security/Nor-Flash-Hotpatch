#include "patch_result.h"

bool patch_result_is_fixed(int ret_code)
{
    return ret_code == -1 ||
           ret_code == -2 ||
           ret_code == PATCH_RESULT_ATTACK_BLOCKED ||
           ret_code == PATCH_RESULT_REJECT_DNS_BOUNDS ||
           ret_code == PATCH_RESULT_REJECT_ICMP_FAMILY ||
           ret_code == PATCH_RESULT_SAFE_EXECUTED;
}

bool patch_result_is_vulnerable(int ret_code)
{
    return ret_code == PATCH_RESULT_ATTACK_OVERFLOW ||
           ret_code == PATCH_RESULT_ATTACK_DNS_OOB_READ ||
           ret_code == PATCH_RESULT_ATTACK_ICMP_MISDISPATCH;
}

const char *patch_result_name(int ret_code)
{
    if (ret_code == PATCH_RESULT_ATTACK_BLOCKED) {
        return "blocked";
    }
    if (ret_code == PATCH_RESULT_REJECT_DNS_BOUNDS) {
        return "reject:dns-bounds";
    }
    if (ret_code == PATCH_RESULT_REJECT_ICMP_FAMILY) {
        return "reject:family";
    }
    if (ret_code == PATCH_RESULT_ATTACK_OVERFLOW) {
        return "overflow";
    }
    if (ret_code == PATCH_RESULT_ATTACK_DNS_OOB_READ) {
        return "dns-oob-read";
    }
    if (ret_code == PATCH_RESULT_ATTACK_ICMP_MISDISPATCH) {
        return "icmp-misdispatch";
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
