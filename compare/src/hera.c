/*
 * hera.c — HERA comparison implementation.
 *
 * Uses the ARM Cortex-M4 Flash Patch and Breakpoint (FPB) unit to redirect
 * a function entry to a RAM-resident dispatcher.  The FPB replaces the first
 * instruction of the target function with a literal-pool load (LDR PC) that
 * jumps to the HERA RAM dispatcher, which then executes the patched code.
 */
#include "compare.h"

#include <string.h>

#include "SEGGER_RTT.h"

#include "core/app/app_mode.h"
#include "core/console/console.h"
#include "core/target/cve_target.h"
#include "core/target/targets.h"
#include "nrf.h"

#define HERA_FPB_SLOT                0u
#define HERA_FPB_CTRL_ENABLE_Msk     0x1u
#define HERA_FPB_CTRL_KEY_Msk        0x2u
#define HERA_FPB_LDR_PC_LITERAL_WORD 0xF000F8DFu

typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t REMAP;
    volatile uint32_t COMP[128];
} hera_fpb_unit_t;

typedef bool (*hera_exec_mode_is_verbose_fn_t)(void);
typedef bool (*hera_fetch_inputs_fn_t)(cve_target_input_t *input, bool *auto_fed);
typedef int (*hera_target_run_fn_t)(const cve_target_input_t *input,
                                    bool verbose,
                                    const cve_target_profile_t *profile);

typedef struct {
    hera_exec_mode_is_verbose_fn_t exec_mode_is_verbose;
    hera_fetch_inputs_fn_t fetch_inputs;
    hera_target_run_fn_t target_run;
} hera_runtime_api_t;

extern uint32_t __hera_ram_text_start__;
extern uint32_t __hera_ram_text_end__;
extern uint32_t __hera_ram_text_load_start__;

static hera_fpb_unit_t *const g_hera_fpb = (hera_fpb_unit_t *)0xE0002000u;
static bool g_hera_fpb_initialized = false;
static volatile hera_runtime_api_t g_hera_api;
static uint32_t g_hera_fpb_remap_table[8] __attribute__((aligned(32))) = {0};

static const cve_target_profile_t g_hera_profile = {
    .banner = "\r\n=== [HERA] RAM Hotpatch Replacement ===\r\n",
    .status_line = "Status: RAM replacement fix for the selected vulnerability is ENABLED.\r\n",
    .reject_prefix = "HERA",
    .block_line = "[HERA] RAM hotpatch blocked the crafted attack input.\r\n",
    .abort_line = "[HERA] Returning to shell without entering the vulnerable path.\r\n",
    .done_line = "\r\n[*] HERA patched path finished. Returning to shell...\r\n",
    .apply_fix = true,
};

static size_t hera_ram_text_size(void) {
    return (size_t)((uintptr_t)&__hera_ram_text_end__ - (uintptr_t)&__hera_ram_text_start__);
}

static uintptr_t hera_patch_point_addr(void) {
    return ((uintptr_t)fun1) & ~(uintptr_t)1u;
}

static uintptr_t hera_dispatcher_addr(void) {
    return ((uintptr_t)hera_ram_dispatcher) | (uintptr_t)1u;
}

static void fpb_init_once(void) {
    if (g_hera_fpb_initialized) {
        return;
    }

    memset(g_hera_fpb_remap_table, 0, sizeof(g_hera_fpb_remap_table));
    g_hera_fpb->CTRL = HERA_FPB_CTRL_KEY_Msk;
    g_hera_fpb->REMAP = (uint32_t)(uintptr_t)g_hera_fpb_remap_table;
    g_hera_fpb->COMP[HERA_FPB_SLOT] = 0u;

    __DSB();
    __ISB();

    g_hera_fpb_initialized = true;
}

static void fpb_install_matcher(uintptr_t patch_point_addr, uint32_t remap_word) {
    g_hera_fpb->CTRL = HERA_FPB_CTRL_KEY_Msk;
    g_hera_fpb->COMP[HERA_FPB_SLOT] = 0u;
    g_hera_fpb_remap_table[HERA_FPB_SLOT] = remap_word;
    g_hera_fpb->REMAP = (uint32_t)(uintptr_t)g_hera_fpb_remap_table;
    g_hera_fpb->COMP[HERA_FPB_SLOT] = ((uint32_t)patch_point_addr & ~0x3u) | 0x1u;

    __DSB();
    __ISB();
}

static void fpb_enable_matcher(void) {
    g_hera_fpb->CTRL = HERA_FPB_CTRL_KEY_Msk | HERA_FPB_CTRL_ENABLE_Msk;

    __DSB();
    __ISB();
}

static void fpb_disable_matcher(void) {
    g_hera_fpb->COMP[HERA_FPB_SLOT] = 0u;
    g_hera_fpb->CTRL = HERA_FPB_CTRL_KEY_Msk;

    __DSB();
    __ISB();
}

static bool fpb_matcher_is_enabled(void) {
    return g_hera_fpb_initialized
        && ((g_hera_fpb->CTRL & HERA_FPB_CTRL_ENABLE_Msk) != 0u)
        && ((g_hera_fpb->COMP[HERA_FPB_SLOT] & 0x1u) != 0u);
}

static void hera_copy_ram_text(void) {
    memcpy((void *)&__hera_ram_text_start__, (const void *)&__hera_ram_text_load_start__, hera_ram_text_size());

    __DSB();
    __ISB();
}

static __attribute__((section(".hera_ram_text.payload"), noinline, used))
int hera_ram_patch_payload(void) {
    cve_target_input_t input = {0};
    bool auto_fed = false;
    bool verbose = g_hera_api.exec_mode_is_verbose();

    if (!g_hera_api.fetch_inputs(&input, &auto_fed)) {
        return -127;
    }

    if (verbose && auto_fed) {
        console_puts("[DEMO] Loaded attack input for the selected vulnerability.\r\n");
    }

    return g_hera_api.target_run(&input, verbose, &g_hera_profile);
}

__attribute__((section(".hera_ram_text.dispatcher"), noinline, used))
int hera_ram_dispatcher(void) {
    return hera_ram_patch_payload();
}

bool hera_patch_install(void) {
    if ((hera_patch_point_addr() & 0x3u) != 0u) {
        console_puts("[-] HERA patch point is not word aligned for FPB remap.\r\n");
        return false;
    }

    if (((uintptr_t)g_hera_fpb_remap_table & 0x1Fu) != 0u) {
        console_puts("[-] HERA remap table is not 32-byte aligned.\r\n");
        return false;
    }

    if (hera_ram_text_size() == 0u) {
        console_puts("[-] HERA RAM text section is empty.\r\n");
        return false;
    }

    fpb_init_once();

    g_hera_api.exec_mode_is_verbose = app_exec_mode_is_verbose;
    g_hera_api.fetch_inputs = cve_target_fetch_inputs;
    g_hera_api.target_run = cve_target_run;

    hera_copy_ram_text();
    fpb_install_matcher(hera_patch_point_addr(), HERA_FPB_LDR_PC_LITERAL_WORD);
    fpb_enable_matcher();
    return true;
}

void hera_patch_unapply(void) {
    fpb_disable_matcher();
}

bool hera_patch_is_active(void) {
    return fpb_matcher_is_enabled();
}

int hera_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    (void)r0;
    (void)r1;
    (void)r2;
    (void)r3;
    return fun1();
}

void hera_patch_print_status(void) {
    SEGGER_RTT_printf(0,
        "[hera] active=%s patch_point=0x%08X dispatcher=0x%08X payload=0x%08X remap=0x%08X target=%s\r\n",
        hera_patch_is_active() ? "yes" : "no",
        (uint32_t)hera_patch_point_addr(),
        (uint32_t)(hera_dispatcher_addr() & ~(uintptr_t)1u),
        (uint32_t)(((uintptr_t)hera_ram_patch_payload) & ~(uintptr_t)1u),
        (uint32_t)(uintptr_t)g_hera_fpb_remap_table,
        cve_target_name(cve_target_get_current()));
}
