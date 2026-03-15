#include "app_common.h"
#include "autopatch_mode.h"
#include "patch_control.h"
#include "hera_patch.h"
#include "rapidpatch_vm.h"

#include <limits.h>

#include "nrf.h"

#define LEGACY_ORIGINAL_HALFWORD 0xE7FFu
#define LEGACY_UNPATCH_HALFWORD  0xE000u
#define RAPIDPATCH_MAX_CODE_SIZE 192u

typedef struct {
    bool active;
    uint32_t install_addr;
    uint16_t code_len;
    uint8_t code[RAPIDPATCH_MAX_CODE_SIZE];
    rapidpatch_vm_t vm;
} rapidpatch_context_t;

static rapidpatch_context_t g_rapid_ctx = {0};

const char *patch_scheme_name(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        return "rapid";
    }
    if (scheme == PATCH_SCHEME_HERA) {
        return "hera";
    }
    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        return "autopatch";
    }
    return "legacy";
}

uintptr_t patch_slot_addr(void) {
    return ((uintptr_t)patch_slot) & ~(uintptr_t)1u;
}

uint32_t rapid_patch_install_addr(void) {
    return (uint32_t)(((uintptr_t)rapid_vuln_target) & ~(uintptr_t)1u);
}

static void invalidate_code_cache(void) {
#if defined(NVMC_FEATURE_CACHE_PRESENT)
    uint32_t icache = NRF_NVMC->ICACHECNF;

    NRF_NVMC->ICACHECNF =
        (icache & ~NVMC_ICACHECNF_CACHEEN_Msk) |
        (NVMC_ICACHECNF_CACHEEN_Disabled << NVMC_ICACHECNF_CACHEEN_Pos);

    __DSB();
    __ISB();

    NRF_NVMC->ICACHECNF = icache;

    __DSB();
    __ISB();
#endif
}

static bool build_legacy_patch_branch_instr(uint16_t *out_instr, bool verbose) {
    uintptr_t from = patch_slot_addr();
    uintptr_t to   = ((uintptr_t)fun2) & ~(uintptr_t)1u;
    int32_t diff   = (int32_t)to - (int32_t)(from + 4u);

    if ((diff & 1) != 0) {
        if (verbose) {
            console_puts("[-] patch target is not halfword aligned.\r\n");
        }
        return false;
    }

    if (diff < -2048 || diff > 2046) {
        if (verbose) {
            SEGGER_RTT_printf(0,
                "[-] fun2 entry out of 16-bit Thumb B range. diff=%d bytes\r\n",
                diff);
        }
        return false;
    }

    *out_instr = (uint16_t)(0xE000u | (((uint32_t)(diff >> 1)) & 0x07FFu));
    return true;
}

static void write_patch_halfword(uint16_t instr) {
    uintptr_t patch_addr   = patch_slot_addr();
    uint32_t aligned_addr  = (uint32_t)(patch_addr & ~(uintptr_t)3u);
    volatile uint32_t *fw  = (volatile uint32_t *)aligned_addr;

    uint32_t old_val = *fw;
    uint32_t new_val;

    if ((patch_addr & 2u) == 0u) {
        new_val = (old_val & 0xFFFF0000u) | instr;
    } else {
        new_val = (old_val & 0x0000FFFFu) | ((uint32_t)instr << 16);
    }

    NRF_NVMC->CONFIG = 1;
    while (NRF_NVMC->READY == 0) {
    }

    *fw = new_val;
    while (NRF_NVMC->READY == 0) {
    }

    NRF_NVMC->CONFIG = 0;
    while (NRF_NVMC->READY == 0) {
    }

    __DSB();
    __ISB();
    invalidate_code_cache();
}

uint16_t read_patch_halfword(void) {
    uintptr_t patch_addr  = patch_slot_addr();
    uint32_t aligned_addr = (uint32_t)(patch_addr & ~(uintptr_t)3u);
    uint32_t value        = *(volatile uint32_t *)aligned_addr;

    if ((patch_addr & 2u) == 0u) {
        return (uint16_t)(value & 0xFFFFu);
    }
    return (uint16_t)(value >> 16);
}

static bool legacy_patch_apply(void) {
    uint16_t branch_instr = 0;

    if (!build_legacy_patch_branch_instr(&branch_instr, true)) {
        return false;
    }

    uint16_t current = read_patch_halfword();
    if ((current & branch_instr) != branch_instr) {
        SEGGER_RTT_printf(0,
            "[-] Cannot apply legacy patch without erase. current=0x%04X target=0x%04X\r\n",
            current,
            branch_instr);
        return false;
    }

    write_patch_halfword(branch_instr);
    return true;
}

static void legacy_patch_unapply(void) {
    write_patch_halfword(LEGACY_UNPATCH_HALFWORD);
}

static bool legacy_patch_is_active(void) {
    uint16_t branch_instr = 0;

    if (!build_legacy_patch_branch_instr(&branch_instr, false)) {
        return false;
    }

    return read_patch_halfword() == branch_instr;
}

static bool legacy_patch_demo_can_run(void) {
    return read_patch_halfword() == LEGACY_ORIGINAL_HALFWORD;
}

static bool rapid_patch_install(void) {
    uint16_t code_len = rapid_patch_code_size();

    if (code_len == 0u || code_len > RAPIDPATCH_MAX_CODE_SIZE) {
        SEGGER_RTT_printf(0,
            "[-] RapidPatch code size %u is invalid for the local runtime.\r\n",
            code_len);
        return false;
    }

    memcpy(g_rapid_ctx.code, rapid_patch_code_bytes(), code_len);
    if (!rapidpatch_vm_init(&g_rapid_ctx.vm, g_rapid_ctx.code, code_len)) {
        console_puts("[-] RapidPatch VM init failed.\r\n");
        return false;
    }

    g_rapid_ctx.install_addr = rapid_patch_install_addr();
    g_rapid_ctx.code_len = code_len;
    g_rapid_ctx.active = true;
    return true;
}

static void rapid_patch_unapply(void) {
    memset(&g_rapid_ctx, 0, sizeof(g_rapid_ctx));
}

static bool rapid_patch_is_active(void) {
    return g_rapid_ctx.active;
}

int rapid_fixed_patch_point_invoke(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    rapidpatch_fixed_frame_t frame = {
        .r0 = r0,
        .r1 = r1,
        .r2 = r2,
        .r3 = r3,
        .lr = g_rapid_ctx.install_addr,
    };

    if (!g_rapid_ctx.active) {
        return (int)RAPIDPATCH_FIXED_OP_PASS;
    }

    uint64_t ret = rapidpatch_vm_exec(&g_rapid_ctx.vm, &frame, sizeof(frame));
    if (ret == UINT64_MAX) {
        if (app_exec_mode_is_verbose()) {
            console_puts("[-] RapidPatch VM execution failed.\r\n");
        }
        return -127;
    }

    uint32_t op = (uint32_t)(ret >> 32);
    uint32_t ret_code = (uint32_t)ret;

    if (op == RAPIDPATCH_FILTER_DROP || op == RAPIDPATCH_FILTER_REDIRECT) {
        return (int32_t)ret_code;
    }

    return (int)RAPIDPATCH_FIXED_OP_PASS;
}

int rapid_patch_slot(void) {
    UBaseType_t uxQueueLength = 0;
    UBaseType_t uxItemSize = 0;
    int ret = 0;
    bool verbose = app_exec_mode_is_verbose();

    if (verbose) {
        console_puts("\r\n=== [RAPIDPATCH] Fixed Patch Point Entry ===\r\n");
    }

    if (app_fetch_auto_inputs(&uxQueueLength, &uxItemSize)) {
        if (verbose) {
            console_puts("[DEMO] Auto-fed triggering input values.\r\n");
        }
    } else {
        PROMPT_RTT_U32("Enter uxQueueLength: ", uxQueueLength);
        PROMPT_RTT_U32("Enter uxItemSize: ", uxItemSize);
    }

    ret = rapid_vuln_target(uxQueueLength, uxItemSize);
    if (verbose && (ret == -1 || ret == -2 || ret == -3)) {
        SEGGER_RTT_printf(0,
            "\r\n[RAPIDPATCH] Filter rejected request at fixed patch point, ret=%d\r\n",
            ret);
        if (ret == -3) {
            console_puts("[RAPIDPATCH] Prevented integer wraparound before the vulnerable body executed.\r\n");
        }
        console_puts("[RAPIDPATCH] Original vulnerable body was skipped.\r\n");
        return ret;
    }

    if (verbose && ret != 0) {
        SEGGER_RTT_printf(0, "\r\n[RAPIDPATCH] Target returned %d\r\n", ret);
    }

    return ret;
}

int patch_call(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        return rapid_patch_slot();
    }
    if (scheme == PATCH_SCHEME_HERA) {
        return hera_patch_slot();
    }
    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        return autopatch_patch_slot();
    }
    return patch_slot();
}

bool patch_apply(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        return rapid_patch_install();
    }
    if (scheme == PATCH_SCHEME_HERA) {
        return hera_patch_install();
    }
    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        return autopatch_set_enabled(true);
    }
    return legacy_patch_apply();
}

void patch_unapply(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        rapid_patch_unapply();
    } else if (scheme == PATCH_SCHEME_HERA) {
        hera_patch_unapply();
    } else if (scheme == PATCH_SCHEME_AUTOPATCH) {
        (void)autopatch_set_enabled(false);
    } else {
        legacy_patch_unapply();
    }
}

bool patch_is_active(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        return rapid_patch_is_active();
    }
    if (scheme == PATCH_SCHEME_HERA) {
        return hera_patch_is_active();
    }
    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        return autopatch_is_enabled();
    }
    return legacy_patch_is_active();
}

bool patch_demo_can_run(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        return true;
    }
    if (scheme == PATCH_SCHEME_HERA) {
        return true;
    }
    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        return autopatch_is_ready();
    }
    return legacy_patch_demo_can_run();
}

bool patch_supports_online_toggle(patch_scheme_t scheme) {
    (void)scheme;
    return true;
}

void print_patch_status(patch_scheme_t scheme) {
    if (scheme == PATCH_SCHEME_RAPID) {
        SEGGER_RTT_printf(0,
            "[rapid] active=%s install_addr=0x%08X code_len=%u bytes\r\n",
            g_rapid_ctx.active ? "yes" : "no",
            g_rapid_ctx.install_addr,
            g_rapid_ctx.code_len);
        return;
    }

    if (scheme == PATCH_SCHEME_HERA) {
        SEGGER_RTT_printf(0,
            "[hera] active=%s payload_addr=0x%08X\r\n",
            hera_patch_is_active() ? "yes" : "no",
            (uint32_t)hera_patch_payload_addr());
        return;
    }

    if (scheme == PATCH_SCHEME_AUTOPATCH) {
        autopatch_print_status();
        return;
    }

    uint16_t instr = read_patch_halfword();
    uint16_t branch_instr = 0;
    bool has_branch = build_legacy_patch_branch_instr(&branch_instr, false);

    SEGGER_RTT_printf(0, "[legacy] patch_slot first halfword: 0x%04X\r\n", instr);

    if (has_branch && instr == branch_instr) {
        console_puts("[legacy] mode: redirect to fun2\r\n");
    } else if (instr == LEGACY_ORIGINAL_HALFWORD) {
        console_puts("[legacy] mode: original entry (fun1 path)\r\n");
    } else if (instr == LEGACY_UNPATCH_HALFWORD) {
        console_puts("[legacy] mode: unpatched forward jump (fun1 path)\r\n");
    } else {
        console_puts("[legacy] mode: unknown\r\n");
    }
}

void print_all_patch_status(void) {
    print_patch_status(PATCH_SCHEME_RAPID);
    print_patch_status(PATCH_SCHEME_HERA);
    print_patch_status(PATCH_SCHEME_LEGACY);
    print_patch_status(PATCH_SCHEME_AUTOPATCH);
}

__attribute__((naked, noinline, used, section(".hotpatch_page.slot"), aligned(2)))
int patch_slot(void) {
    __asm volatile(
        ".thumb        \n"
        ".hword 0xE7FF \n"
        "nop           \n"
        "b   fun1      \n"
    );
}
