#include "cve_target.h"

#include <string.h>

#include "../app/app_mode.h"
#include "../console/console.h"
#include "cve_target_impl.h"

static const UBaseType_t g_cve2024_2212_queue_length = 0x40000001u;
static const UBaseType_t g_cve2024_2212_item_size = 0x00000004u;

static cve_target_t g_current_target = CVE_TARGET_CVE2024_2212;

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
    console_printf("[target] current vulnerability: %s\r\n", cve_target_name(g_current_target));
}

void cve_target_get_attack_inputs(cve_target_input_t *input) {
    if (input == NULL) {
        return;
    }

    input->target = g_current_target;
    input->queue_length = 0u;
    input->item_size = 0u;

    if (g_current_target == CVE_TARGET_CVE2024_2212) {
        input->queue_length = g_cve2024_2212_queue_length;
        input->item_size = g_cve2024_2212_item_size;
    }
}

bool cve_target_fetch_inputs(cve_target_input_t *input, bool *auto_fed) {
    bool fed = app_get_exec_mode() != APP_EXEC_MODE_INTERACTIVE;

    if (input == NULL) {
        return false;
    }

    cve_target_get_attack_inputs(input);

    if (g_current_target == CVE_TARGET_CVE2024_2212 && !fed) {
        if (!console_prompt_u32("Enter uxQueueLength: ", &input->queue_length)) {
            return false;
        }
        if (!console_prompt_u32("Enter uxItemSize: ", &input->item_size)) {
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
        console_printf(
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
        console_printf(
            "\r\n[Input]\r\n"
            "  target         = %s\r\n"
            "  msg_size       = 18\r\n"
            "  answer_offset  = 12\r\n"
            "  dname_len      = 3\r\n"
            "  secret_len     = 10\r\n",
            cve_target_name(target));
        return;
    }

    console_printf(
        "\r\n[Input]\r\n"
        "  target             = %s\r\n"
        "  packet family      = IPv4\r\n"
        "  registered handler = IPv6 echo request\r\n"
        "  icmp type/code     = 128/0\r\n"
        "  adjacent bytes     = 32\r\n",
        cve_target_name(target));
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
