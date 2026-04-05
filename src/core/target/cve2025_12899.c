#include "cve_target_impl.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../console/console.h"
#include "../patch/patch_result.h"

typedef enum {
    ICMP_FAMILY_IPV4 = 4,
    ICMP_FAMILY_IPV6 = 6,
} icmp_family_t;

typedef struct {
    uint8_t vhl;
    uint8_t tos;
    uint16_t total_len;
    uint32_t src;
    uint32_t dst;
    uint8_t rest[8];
} ipv4_hdr_t;

typedef struct {
    uint32_t vtc_flow;
    uint16_t payload_len;
    uint8_t next_header;
    uint8_t hop_limit;
    uint8_t src[16];
    uint8_t dst[16];
} ipv6_hdr_t;

typedef struct {
    uint8_t type;
    uint8_t code;
} icmp_hdr_t;

typedef int (*handler_fn_t)(icmp_family_t pkt_family, const void *ip_hdr, const icmp_hdr_t *icmp);

typedef struct {
    icmp_family_t family;
    uint8_t type;
    uint8_t code;
    handler_fn_t handler;
} icmp_ctx_t;

enum {
    ICMP_HANDLER_LIMIT = 4u,
    ICMP_ADJACENT_SIZE = 32u,
    ICMP_BACKING_SIZE = sizeof(ipv4_hdr_t) + ICMP_ADJACENT_SIZE,
};

static const char *reject_prefix_from_profile(const cve_target_profile_t *profile) {
    if (profile == NULL || profile->reject_prefix == NULL || profile->reject_prefix[0] == '\0') {
        return "FIX";
    }

    return profile->reject_prefix;
}

static void icmp_prepare_case(uint8_t backing[ICMP_BACKING_SIZE], icmp_hdr_t *icmp) {
    ipv4_hdr_t *ipv4 = NULL;

    memset(backing, 0, ICMP_BACKING_SIZE);

    ipv4 = (ipv4_hdr_t *)backing;
    ipv4->vhl = 0x45u;
    ipv4->tos = 0x00u;
    ipv4->total_len = 28u;
    ipv4->src = 0x01020304u;
    ipv4->dst = 0x05060708u;

    for (size_t i = 0; i < ICMP_ADJACENT_SIZE; ++i) {
        backing[sizeof(ipv4_hdr_t) + i] = (uint8_t)(0xA0u + (uint8_t)i);
    }

    icmp->type = 128u;
    icmp->code = 0u;
}

static void register_handler(icmp_ctx_t handlers[ICMP_HANDLER_LIMIT],
                             size_t *handler_count,
                             icmp_family_t family,
                             uint8_t type,
                             uint8_t code,
                             handler_fn_t handler) {
    if (handlers == NULL || handler_count == NULL || *handler_count >= ICMP_HANDLER_LIMIT) {
        return;
    }

    handlers[*handler_count].family = family;
    handlers[*handler_count].type = type;
    handlers[*handler_count].code = code;
    handlers[*handler_count].handler = handler;
    (*handler_count)++;
}

static int icmpv6_echo_handler(icmp_family_t pkt_family, const void *ip_hdr, const icmp_hdr_t *icmp) {
    const ipv6_hdr_t *hdr = (const ipv6_hdr_t *)ip_hdr;

    (void)pkt_family;
    (void)icmp;

    console_printf(
        "[icmpv6 handler] next_header=%u hop_limit=%u src[0..3]=%02X %02X %02X %02X\r\n",
        hdr->next_header,
        hdr->hop_limit,
        hdr->src[0],
        hdr->src[1],
        hdr->src[2],
        hdr->src[3]);

    return PATCH_RESULT_ATTACK_ICMP_MISDISPATCH;
}

static int icmp_call_handlers_vuln(icmp_family_t pkt_family,
                                   const void *ip_hdr,
                                   const icmp_hdr_t *icmp,
                                   const icmp_ctx_t handlers[ICMP_HANDLER_LIMIT],
                                   size_t handler_count) {
    (void)pkt_family;

    for (size_t i = 0; i < handler_count; ++i) {
        if (handlers[i].type == icmp->type &&
            (handlers[i].code == icmp->code || handlers[i].code == 0u)) {
            return handlers[i].handler(pkt_family, ip_hdr, icmp);
        }
    }

    return PATCH_RESULT_REJECT_ICMP_FAMILY;
}

static int icmp_call_handlers_fixed(icmp_family_t pkt_family,
                                    const void *ip_hdr,
                                    const icmp_hdr_t *icmp,
                                    const icmp_ctx_t handlers[ICMP_HANDLER_LIMIT],
                                    size_t handler_count) {
    for (size_t i = 0; i < handler_count; ++i) {
        if (handlers[i].family != pkt_family) {
            continue;
        }

        if (handlers[i].type == icmp->type &&
            (handlers[i].code == icmp->code || handlers[i].code == 0u)) {
            return handlers[i].handler(pkt_family, ip_hdr, icmp);
        }
    }

    return PATCH_RESULT_REJECT_ICMP_FAMILY;
}

int cve2025_12899_run(const cve_target_input_t *input,
                      bool verbose,
                      const cve_target_profile_t *profile) {
    uint8_t backing[ICMP_BACKING_SIZE];
    icmp_hdr_t icmp = {0};
    icmp_ctx_t handlers[ICMP_HANDLER_LIMIT] = {0};
    size_t handler_count = 0u;
    int ret = PATCH_RESULT_SAFE_NOOP;

    if (input == NULL || profile == NULL) {
        return -127;
    }

    icmp_prepare_case(backing, &icmp);
    register_handler(handlers, &handler_count, ICMP_FAMILY_IPV6, 128u, 0u, icmpv6_echo_handler);

    if (verbose) {
        console_puts(profile->banner);
        console_printf("Target: %s\r\n", cve_target_name(input->target));
        console_puts(profile->status_line);
        cve_target_print_attack_input(input);
    }

    if (profile->apply_fix) {
        ret = icmp_call_handlers_fixed(ICMP_FAMILY_IPV4, backing, &icmp, handlers, handler_count);
        if (ret == PATCH_RESULT_REJECT_ICMP_FAMILY) {
            if (verbose) {
                console_printf(
                    "\r\n[%s] Family-aware dispatch rejected the IPv4 -> IPv6 handler mismatch, ret=%d\r\n",
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

    if (verbose) {
        console_puts("\r\n[vuln] dispatching IPv4 packet with ICMP type 128\r\n");
    }

    ret = icmp_call_handlers_vuln(ICMP_FAMILY_IPV4, backing, &icmp, handlers, handler_count);

    if (verbose) {
        console_puts("  [-] IPv4 packet was mis-dispatched into an IPv6 handler.\r\n");
        if (profile->done_line != NULL && profile->done_line[0] != '\0') {
            console_puts(profile->done_line);
        }
    }

    return ret;
}

bool cve2025_12899_evaluate_filter(const cve_target_input_t *input,
                                   cve_target_filter_result_t *result) {
    uint8_t backing[ICMP_BACKING_SIZE];
    icmp_hdr_t icmp = {0};
    icmp_ctx_t handlers[ICMP_HANDLER_LIMIT] = {0};
    size_t handler_count = 0u;
    int ret = PATCH_RESULT_SAFE_NOOP;

    if (input == NULL || result == NULL) {
        return false;
    }

    icmp_prepare_case(backing, &icmp);
    register_handler(handlers, &handler_count, ICMP_FAMILY_IPV6, 128u, 0u, icmpv6_echo_handler);
    ret = icmp_call_handlers_fixed(ICMP_FAMILY_IPV4, backing, &icmp, handlers, handler_count);

    result->op = (ret == PATCH_RESULT_REJECT_ICMP_FAMILY) ? CVE_FILTER_DROP : CVE_FILTER_PASS;
    result->ret_code = (ret == PATCH_RESULT_REJECT_ICMP_FAMILY) ? ret : PATCH_RESULT_SAFE_NOOP;
    return true;
}
