#ifndef COMPARE_COMPARE_H
#define COMPARE_COMPARE_H

#include <stdbool.h>
#include <stdint.h>

#include "core/patch/clearbit_patch.h"
#include "core/platform/memory_cost.h"

typedef struct {
    const char *name;
    bool (*install)(void);
    void (*uninstall)(void);
    bool (*is_active)(void);
    int (*invoke)(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);
} scheme_ops_t;

/* clearbit_patch_unapply / clearbit_patch_is_active / patch_slot
   are provided by core/patch/clearbit_patch.h above. */
bool clearbit_patch_install(void);
int clearbit_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);

bool autopatch_is_ready(void);
bool autopatch_patch_install(void);
void autopatch_patch_unapply(void);
bool autopatch_patch_is_active(void);
int autopatch_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);
int autopatch_patch_slot(void);
void autopatch_print_status(void);
uint32_t autopatch_bench_trigger_only(void);
uint32_t autopatch_bench_dispatch_only(uint32_t active_patch_count);
uint32_t autopatch_bench_patch_exec_only(void);

bool hera_patch_install(void);
void hera_patch_unapply(void);
bool hera_patch_is_active(void);
int hera_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);
void hera_patch_print_status(void);
int hera_ram_dispatcher(void);

bool rapid_patch_install(void);
void rapid_patch_unapply(void);
bool rapid_patch_is_active(void);
int rapid_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);
void rapid_patch_print_status(void);
int rapid_patch_slot(void);
int rapid_fixed_patch_point_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);
uint32_t rapid_bench_trigger_only(void);
uint32_t rapid_bench_dispatch_only(uint32_t active_patch_count);
uint32_t rapid_bench_patch_exec_only(void);

memory_cost_t clearbit_memory_cost(void);
memory_cost_t rapid_memory_cost(void);
memory_cost_t hera_memory_cost(void);
memory_cost_t autopatch_memory_cost(void);

#endif
