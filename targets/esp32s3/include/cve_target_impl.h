#ifndef CVE_TARGET_IMPL_H
#define CVE_TARGET_IMPL_H

#include "cve_target.h"

int cve2024_2212_run(const cve_target_input_t *input,
                     bool verbose,
                     const cve_target_profile_t *profile);
bool cve2024_2212_evaluate_filter(const cve_target_input_t *input,
                                  cve_target_filter_result_t *result);

int cve2025_1674_run(const cve_target_input_t *input,
                     bool verbose,
                     const cve_target_profile_t *profile);
bool cve2025_1674_evaluate_filter(const cve_target_input_t *input,
                                  cve_target_filter_result_t *result);

int cve2025_12899_run(const cve_target_input_t *input,
                      bool verbose,
                      const cve_target_profile_t *profile);
bool cve2025_12899_evaluate_filter(const cve_target_input_t *input,
                                   cve_target_filter_result_t *result);

#endif
