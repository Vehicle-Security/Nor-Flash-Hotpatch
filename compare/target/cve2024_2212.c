#include "cve_target_impl.h"

#include <stddef.h>

#include "queue_demo.h"

#include "core/patch/patch_result.h"

static const char *reject_prefix_from_profile(const cve_target_profile_t *profile) {
    if (profile == NULL || profile->reject_prefix == NULL || profile->reject_prefix[0] == '\0') {
        return "FIX";
    }

    return profile->reject_prefix;
}

int cve2024_2212_run(const cve_target_input_t *input,
                     bool verbose,
                     const cve_target_profile_t *profile) {
    queue_demo_profile_t queue_profile = {
        .banner = (profile != NULL && profile->banner != NULL) ? profile->banner : "",
        .target_name = cve_target_name(CVE_TARGET_CVE2024_2212),
        .status_line = (profile != NULL && profile->status_line != NULL) ? profile->status_line : "",
        .reject_prefix = reject_prefix_from_profile(profile),
        .reject_wrap_line = (profile != NULL && profile->block_line != NULL) ? profile->block_line : "",
        .reject_abort_line = (profile != NULL && profile->abort_line != NULL) ? profile->abort_line : "",
        .done_line = (profile != NULL && profile->done_line != NULL) ? profile->done_line : "",
        .validate_before_alloc = (profile != NULL) ? profile->apply_fix : false,
    };

    if (input == NULL || profile == NULL) {
        return -127;
    }

    return queue_demo_run(input->queue_length, input->item_size, verbose, &queue_profile);
}

bool cve2024_2212_evaluate_filter(const cve_target_input_t *input,
                                  cve_target_filter_result_t *result) {
    int32_t ret_code = PATCH_RESULT_SAFE_NOOP;
    uint32_t op = CVE_FILTER_PASS;

    if (input == NULL || result == NULL) {
        return false;
    }

    if (input->queue_length == 0u) {
        op = CVE_FILTER_DROP;
        ret_code = -1;
    } else if (input->item_size == 0u) {
        op = CVE_FILTER_DROP;
        ret_code = -2;
    } else if (input->queue_length > (0xFFFFFFFFu / input->item_size)) {
        op = CVE_FILTER_DROP;
        ret_code = PATCH_RESULT_ATTACK_BLOCKED;
    }

    result->op = op;
    result->ret_code = ret_code;
    return true;
}
