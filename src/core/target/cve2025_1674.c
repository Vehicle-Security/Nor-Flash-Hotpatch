#include "cve_target_impl.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../console/console.h"
#include "../patch/patch_result.h"

typedef struct {
    const uint8_t *msg;
    size_t msg_size;
    size_t answer_offset;
} dns_msg_t;

enum {
    DNS_LOGICAL_SIZE = 18u,
    DNS_SECRET_SIZE = 10u,
    DNS_BACKING_SIZE = DNS_LOGICAL_SIZE + DNS_SECRET_SIZE,
};

static void dns_prepare_case(uint8_t backing[DNS_BACKING_SIZE], dns_msg_t *dns_msg) {
    static const uint8_t logical_pkt[DNS_LOGICAL_SIZE] = {
        0x12, 0x34, 0x81, 0x80,
        0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x03, 'w', 'w', 'w',
        0x00,
        0xFF
    };
    static const uint8_t secret[DNS_SECRET_SIZE] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x41, 0x52, 0x4D, 0x21, 0x99, 0x77
    };

    memset(backing, 0, DNS_BACKING_SIZE);
    memcpy(backing, logical_pkt, sizeof(logical_pkt));
    memcpy(backing + sizeof(logical_pkt), secret, sizeof(secret));

    dns_msg->msg = backing;
    dns_msg->msg_size = sizeof(logical_pkt);
    dns_msg->answer_offset = 12u;
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

static void dns_print_bytes(const char *tag, const uint8_t *bytes, size_t count) {
    console_printf("%s", tag);
    for (size_t i = 0; i < count; ++i) {
        console_printf("%02X ", bytes[i]);
    }
    console_puts("\r\n");
}

int cve2025_1674_run(const cve_target_input_t *input,
                     bool verbose,
                     const cve_target_profile_t *profile) {
    uint8_t backing[DNS_BACKING_SIZE];
    dns_msg_t dns_msg = {0};
    const uint8_t *answer = NULL;
    const uint8_t *bytes = NULL;
    int ret = PATCH_RESULT_SAFE_NOOP;

    if (input == NULL || profile == NULL) {
        return -127;
    }

    dns_prepare_case(backing, &dns_msg);

    if (verbose) {
        console_puts(profile->banner);
        console_printf("Target: %s\r\n", cve_target_name(input->target));
        console_puts(profile->status_line);
        cve_target_print_attack_input(input);
    }

    if (profile->apply_fix) {
        ret = dns_fixed_policy_check(&dns_msg);
        if (ret != PATCH_RESULT_SAFE_EXECUTED) {
            if (verbose) {
                console_printf(
                    "\r\n[%s] DNS answer bounds check rejected the crafted packet, ret=%d\r\n",
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

    answer = dns_msg.msg + dns_msg.answer_offset;
    bytes = &answer[(size_t)answer[0] + 1u];

    if (verbose) {
        console_puts("\r\n[vuln] rem_size omitted answer_offset and read beyond logical msg_size.\r\n");
        dns_print_bytes("[vuln] bytes read after answer name: ", bytes, 10u);
        console_puts("  [-] Adjacent secret bytes were exposed past the logical DNS packet boundary.\r\n");
        if (profile->done_line != NULL && profile->done_line[0] != '\0') {
            console_puts(profile->done_line);
        }
    }

    return PATCH_RESULT_ATTACK_DNS_OOB_READ;
}

bool cve2025_1674_evaluate_filter(const cve_target_input_t *input,
                                  cve_target_filter_result_t *result) {
    uint8_t backing[DNS_BACKING_SIZE];
    dns_msg_t dns_msg = {0};
    int ret = PATCH_RESULT_SAFE_NOOP;

    if (input == NULL || result == NULL) {
        return false;
    }

    dns_prepare_case(backing, &dns_msg);
    ret = dns_fixed_policy_check(&dns_msg);

    result->op = (ret == PATCH_RESULT_SAFE_EXECUTED) ? CVE_FILTER_PASS : CVE_FILTER_DROP;
    result->ret_code = (ret == PATCH_RESULT_SAFE_EXECUTED) ? PATCH_RESULT_SAFE_NOOP : ret;
    return true;
}
