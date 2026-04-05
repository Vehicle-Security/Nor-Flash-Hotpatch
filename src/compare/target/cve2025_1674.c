#include "cve_target_impl.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "SEGGER_RTT.h"

#include "compare/console/console.h"
#include "core/patch/patch_result.h"

typedef struct {
    const uint8_t *msg;
    size_t msg_size;
    size_t answer_offset;
} dns_msg_t;

enum {
    DNS_LOGICAL_DEFAULT_SIZE = 18u,
    DNS_LOGICAL_MAX_SIZE = 64u,
    DNS_SECRET_SIZE = 10u,
    DNS_BACKING_SIZE = DNS_LOGICAL_MAX_SIZE + DNS_SECRET_SIZE,
};

static uint32_t dns_runtime_answer_offset(const cve_target_input_t *input) {
    return (input != NULL) ? input->runtime_arg0 : 12u;
}

static uint32_t dns_runtime_msg_size(const cve_target_input_t *input) {
    return (input != NULL) ? input->runtime_arg1 : DNS_LOGICAL_DEFAULT_SIZE;
}

static uint32_t dns_runtime_dname_len(const cve_target_input_t *input) {
    return (input != NULL) ? input->runtime_arg2 : 3u;
}

static uint32_t dns_runtime_rem_size_hint(const cve_target_input_t *input) {
    return (input != NULL) ? input->runtime_arg3 : 3u;
}

static void dns_prepare_case(uint8_t backing[DNS_BACKING_SIZE],
                             dns_msg_t *dns_msg,
                             uint32_t answer_offset_runtime,
                             uint32_t msg_size_runtime,
                             uint32_t dname_len_runtime) {
    static const uint8_t header_prefix[] = {
        0x12, 0x34, 0x81, 0x80,
        0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
    };
    static const uint8_t secret[DNS_SECRET_SIZE] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x41, 0x52, 0x4D, 0x21, 0x99, 0x77
    };
    size_t msg_size = (size_t)msg_size_runtime;
    size_t answer_offset = (size_t)answer_offset_runtime;
    size_t dname_len = (size_t)dname_len_runtime;

    if (msg_size == 0u || msg_size > DNS_LOGICAL_MAX_SIZE) {
        msg_size = DNS_LOGICAL_DEFAULT_SIZE;
    }
    if (answer_offset >= msg_size) {
        answer_offset = 12u;
    }
    if (dname_len > (msg_size - answer_offset)) {
        dname_len = 3u;
    }

    memset(backing, 0, DNS_BACKING_SIZE);
    memcpy(backing, header_prefix, sizeof(header_prefix));
    backing[answer_offset] = (uint8_t)dname_len;
    if ((answer_offset + dname_len + 1u) < msg_size) {
        backing[answer_offset + dname_len + 1u] = 0xFFu;
    }
    memcpy(backing + DNS_LOGICAL_MAX_SIZE, secret, sizeof(secret));

    dns_msg->msg = backing;
    dns_msg->msg_size = msg_size;
    dns_msg->answer_offset = answer_offset;
}

static const char *reject_prefix_from_profile(const cve_target_profile_t *profile) {
    if (profile == NULL || profile->reject_prefix == NULL || profile->reject_prefix[0] == '\0') {
        return "FIX";
    }

    return profile->reject_prefix;
}

static int dns_fixed_policy_check(const dns_msg_t *dns_msg) {
    const uint8_t *answer = NULL;
    size_t dname_len = 0u;
    size_t rem_size = 0u;

    if (dns_msg == NULL || dns_msg->msg == NULL) {
        return -127;
    }

    if (dns_msg->answer_offset >= dns_msg->msg_size) {
        return PATCH_RESULT_REJECT_DNS_BOUNDS;
    }

    answer = dns_msg->msg + dns_msg->answer_offset;
    dname_len = answer[0];

    if (dname_len > (dns_msg->msg_size - dns_msg->answer_offset)) {
        return PATCH_RESULT_REJECT_DNS_BOUNDS;
    }

    rem_size = dns_msg->msg_size - dns_msg->answer_offset - dname_len;
    if (rem_size < 10u) {
        return PATCH_RESULT_REJECT_DNS_BOUNDS;
    }

    return PATCH_RESULT_SAFE_EXECUTED;
}

static int dns_vuln_path_check(const dns_msg_t *dns_msg) {
    const uint8_t *answer = NULL;
    size_t dname_len = 0u;
    size_t read_end = 0u;

    if (dns_msg == NULL || dns_msg->msg == NULL) {
        return -127;
    }

    if (dns_msg->answer_offset >= dns_msg->msg_size) {
        return PATCH_RESULT_ATTACK_DNS_OOB_READ;
    }

    answer = dns_msg->msg + dns_msg->answer_offset;
    dname_len = answer[0];
    read_end = dns_msg->answer_offset + dname_len + 1u + 10u;
    if (read_end > dns_msg->msg_size) {
        return PATCH_RESULT_ATTACK_DNS_OOB_READ;
    }

    return PATCH_RESULT_SAFE_EXECUTED;
}

static void dns_print_bytes(const char *tag, const uint8_t *bytes, size_t count) {
    SEGGER_RTT_printf(0, "%s", tag);
    for (size_t i = 0; i < count; ++i) {
        SEGGER_RTT_printf(0, "%02X ", bytes[i]);
    }
    console_puts("\r\n");
}

int cve2025_1674_run(const cve_target_input_t *input,
                     bool verbose,
                     const cve_target_profile_t *profile) {
    uint8_t backing[DNS_BACKING_SIZE];
    dns_msg_t dns_msg = {0};
    uint32_t rem_size_hint = 0u;
    const uint8_t *answer = NULL;
    const uint8_t *bytes = NULL;
    int ret = PATCH_RESULT_SAFE_NOOP;

    if (input == NULL || profile == NULL) {
        return -127;
    }

    dns_prepare_case(
        backing,
        &dns_msg,
        dns_runtime_answer_offset(input),
        dns_runtime_msg_size(input),
        dns_runtime_dname_len(input));
    rem_size_hint = dns_runtime_rem_size_hint(input);

    if (verbose) {
        console_puts(profile->banner);
        SEGGER_RTT_printf(0, "Target: %s\r\n", cve_target_name(input->target));
        console_puts(profile->status_line);
        cve_target_print_attack_input(input);
    }

    if (profile->apply_fix) {
        ret = dns_fixed_policy_check(&dns_msg);
        if (ret != PATCH_RESULT_SAFE_EXECUTED) {
            if (verbose) {
                SEGGER_RTT_printf(0,
                    "\r\n[%s] DNS answer bounds check rejected runtime input, ret=%d\r\n",
                    reject_prefix_from_profile(profile),
                    ret);
                if (profile->block_line != NULL && profile->block_line[0] != '\0') {
                    console_puts(profile->block_line);
                }
                if (profile->abort_line != NULL && profile->abort_line[0] != '\0') {
                    console_puts(profile->abort_line);
                }
            }
            return ret;
        }

        if (verbose && profile->done_line != NULL && profile->done_line[0] != '\0') {
            console_puts(profile->done_line);
        }
        return PATCH_RESULT_SAFE_EXECUTED;
    }

    ret = dns_vuln_path_check(&dns_msg);
    if (ret == PATCH_RESULT_ATTACK_DNS_OOB_READ) {
        answer = dns_msg.msg + dns_msg.answer_offset;
        bytes = &answer[(size_t)answer[0] + 1u];
        if (verbose) {
            console_puts("\r\n[vuln] runtime DNS values caused logical OOB read.\r\n");
            SEGGER_RTT_printf(0, "[vuln] rem_size_hint=%lu\r\n", (unsigned long)rem_size_hint);
            dns_print_bytes("[vuln] bytes read after answer name: ", bytes, 10u);
            console_puts("  [-] Adjacent bytes were exposed past logical DNS boundary.\r\n");
            if (profile->done_line != NULL && profile->done_line[0] != '\0') {
                console_puts(profile->done_line);
            }
        }
        return ret;
    }

    if (verbose && profile->done_line != NULL && profile->done_line[0] != '\0') {
        console_puts(profile->done_line);
    }
    return PATCH_RESULT_SAFE_EXECUTED;
}

bool cve2025_1674_evaluate_filter(const cve_target_input_t *input,
                                  cve_target_filter_result_t *result) {
    uint8_t backing[DNS_BACKING_SIZE];
    dns_msg_t dns_msg = {0};
    int ret = PATCH_RESULT_SAFE_NOOP;

    if (input == NULL || result == NULL) {
        return false;
    }

    dns_prepare_case(
        backing,
        &dns_msg,
        dns_runtime_answer_offset(input),
        dns_runtime_msg_size(input),
        dns_runtime_dname_len(input));
    ret = dns_fixed_policy_check(&dns_msg);

    result->op = (ret == PATCH_RESULT_SAFE_EXECUTED) ? CVE_FILTER_PASS : CVE_FILTER_DROP;
    result->ret_code = (ret == PATCH_RESULT_SAFE_EXECUTED) ? PATCH_RESULT_SAFE_NOOP : ret;
    return true;
}
