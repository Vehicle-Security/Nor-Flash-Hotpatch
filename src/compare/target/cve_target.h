#ifndef DEMO_TARGET_CVE_TARGET_H
#define DEMO_TARGET_CVE_TARGET_H

#include <stdbool.h>
#include <stdint.h>

#include "core/app/app_types.h"

typedef enum {
    CVE_TARGET_CVE2024_2212 = 0,
    CVE_TARGET_CVE2025_1674 = 1,
    CVE_TARGET_CVE2025_12899 = 2,
} cve_target_t;

typedef struct {
    const char *banner;
    const char *status_line;
    const char *reject_prefix;
    const char *block_line;
    const char *abort_line;
    const char *done_line;
    bool apply_fix;
} cve_target_profile_t;

typedef struct {
    cve_target_t target;
    UBaseType_t queue_length;
    UBaseType_t item_size;
    uint32_t runtime_arg0;
    uint32_t runtime_arg1;
    uint32_t runtime_arg2;
    uint32_t runtime_arg3;
} cve_target_input_t;

typedef struct {
    uint32_t op;
    int32_t ret_code;
} cve_target_filter_result_t;

enum {
    CVE_FILTER_PASS = 0u,
    CVE_FILTER_DROP = 1u,
    CVE_FILTER_REDIRECT = 2u,
};

const char *cve_target_name(cve_target_t target);
const char *cve_target_cli_name(cve_target_t target);
bool cve_target_parse_name(const char *text, cve_target_t *target);

void cve_target_set_current(cve_target_t target);
cve_target_t cve_target_get_current(void);
void cve_target_print_status(void);

void cve_target_set_benchmark_override_from_regs(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);
void cve_target_clear_benchmark_override(void);
void cve_target_get_attack_inputs(cve_target_input_t *input);
bool cve_target_fetch_inputs(cve_target_input_t *input, bool *auto_fed);
void cve_target_print_attack_input(const cve_target_input_t *input);

int cve_target_run(const cve_target_input_t *input,
                   bool verbose,
                   const cve_target_profile_t *profile);
bool cve_target_evaluate_filter(const cve_target_input_t *input,
                                cve_target_filter_result_t *result);

#endif
