/*
 * benchmark_compare.c — MorphPatch vs EraseRewrite comparison benchmark.
 *
 * Runs both schemes sequentially and prints a side-by-side comparison table.
 * Same timing method as CH32/ESP32 targets: cycle_counter_reset() → apply →
 * first call → cycle_counter_read() = install + first execution.
 */
#include "benchmark_compare.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../app/app_mode.h"
#include "../console/console.h"
#include "../platform/cycle_counter.h"
#include "../patch/patch_control.h"
#include "../patch/patch_result.h"
#include "../patch/morph_patch_erase.h"
#include "../target/cve_target.h"

#define BENCHMARK_PATCHED_CALLS 100u

typedef struct {
    bool available;
    bool apply_ok;
    bool baseline_ok;
    bool first_fix_ok;
    bool fix_ok;
    bool pure_patch_ok;
    bool unfix_ok;
    uint32_t patched_call_count;
    uint32_t t_base_cycles;
    uint32_t t_patch_cycles;
    uint32_t t_fix_first_cycles;
    uint32_t t_fix_cycles;
    uint32_t t_steady_cycles;
    uint32_t t_unfix_cycles;
    uint32_t t_roundtrip_cycles;
    int baseline_ret_code;
    int first_fix_ret_code;
    int fix_ret_code;
    int unfix_ret_code;
} cmp_result_t;

static const char *yes_no(bool v) { return v ? "yes" : "no"; }

static void format_cycles(char *buf, size_t sz, uint32_t c)
{
    if (c == 0xFFFFFFFFu) { (void)snprintf(buf, sz, "N/A"); return; }
    (void)snprintf(buf, sz, "%lu", (unsigned long)c);
}

static void format_result(char *buf, size_t sz, int rc)
{
    (void)snprintf(buf, sz, "%s(%d)", patch_result_name(rc), rc);
}

static uint32_t cycles_delta(uint32_t end, uint32_t start)
{
    if (end == 0xFFFFFFFFu || start == 0xFFFFFFFFu) return 0xFFFFFFFFu;
    return end - start;
}

static uint32_t cycles_add(uint32_t a, uint32_t b)
{
    if (a == 0xFFFFFFFFu || b == 0xFFFFFFFFu) return 0xFFFFFFFFu;
    uint64_t t = (uint64_t)a + (uint64_t)b;
    return (t > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)t;
}

typedef struct {
    const char *name;
    int  (*call)(void);
    bool (*apply)(void);
    void (*unapply)(void);
    bool (*demo_can_run)(void);
} cmp_scheme_t;

static cmp_result_t run_one_scheme(const cmp_scheme_t *s)
{
    cmp_result_t r = {0};
    r.t_base_cycles = r.t_patch_cycles = r.t_fix_first_cycles = 0xFFFFFFFFu;
    r.t_fix_cycles = r.t_steady_cycles = r.t_unfix_cycles = r.t_roundtrip_cycles = 0xFFFFFFFFu;
    r.baseline_ret_code = r.first_fix_ret_code = r.fix_ret_code = r.unfix_ret_code = -999;

    if (!s->demo_can_run()) {
        console_printf("[-] %s requires pristine flash. Reflash.\r\n", s->name);
        return r;
    }

    app_set_exec_mode(APP_EXEC_MODE_BENCHMARK);

    /* Baseline */
    if (!cycle_counter_reset()) { app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE); return r; }
    r.baseline_ret_code = s->call();
    r.t_base_cycles = cycle_counter_read();
    r.baseline_ok = patch_result_is_vulnerable(r.baseline_ret_code);
    if (!r.baseline_ok) {
        console_printf("[-] %s baseline not vulnerable, ret=%d\r\n", s->name, r.baseline_ret_code);
        app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
        return r;
    }

    /* Apply + patched calls */
    if (!cycle_counter_reset()) { app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE); return r; }
    r.apply_ok = s->apply();
    r.pure_patch_ok = r.apply_ok;
    r.t_patch_cycles = cycle_counter_read();

    {
        bool all_fixed = true;
        uint32_t first_fix_end = 0xFFFFFFFFu;
        uint32_t fix_end;

        for (uint32_t i = 0; i < BENCHMARK_PATCHED_CALLS; ++i) {
            int rc = s->call();
            if (i == 0u) { r.first_fix_ret_code = rc; first_fix_end = cycle_counter_read(); }
            r.fix_ret_code = rc;
            r.patched_call_count++;
            if (!patch_result_is_fixed(rc)) { all_fixed = false; break; }
        }
        fix_end = cycle_counter_read();
        r.t_fix_first_cycles = first_fix_end;
        r.t_fix_cycles = fix_end;
        r.t_steady_cycles = cycles_delta(fix_end, first_fix_end);
        r.first_fix_ok = r.apply_ok && patch_result_is_fixed(r.first_fix_ret_code);
        r.fix_ok = r.apply_ok && all_fixed && (r.patched_call_count == BENCHMARK_PATCHED_CALLS);
    }

    /* Unapply */
    if (!cycle_counter_reset()) { s->unapply(); app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE); return r; }
    s->unapply();
    {
        uint32_t unapply_end = cycle_counter_read();
        r.unfix_ret_code = s->call();
        uint32_t unfix_end = cycle_counter_read();
        r.t_unfix_cycles = unfix_end;
        r.t_roundtrip_cycles = cycles_add(r.t_fix_cycles, unapply_end);
    }
    r.unfix_ok = patch_result_is_vulnerable(r.unfix_ret_code);
    r.available = true;

    app_set_exec_mode(APP_EXEC_MODE_INTERACTIVE);
    return r;
}

static void print_scheme_result(const char *name, const cmp_result_t *r)
{
    char base[16], patch[16], fix1[16], fix100[16], steady[16], unfix[16], rt[16];
    char bret[24], f1ret[24], fret[24], uret[24];

    format_cycles(base, sizeof(base), r->t_base_cycles);
    format_cycles(patch, sizeof(patch), r->t_patch_cycles);
    format_cycles(fix1, sizeof(fix1), r->t_fix_first_cycles);
    format_cycles(fix100, sizeof(fix100), r->t_fix_cycles);
    format_cycles(steady, sizeof(steady), r->t_steady_cycles);
    format_cycles(unfix, sizeof(unfix), r->t_unfix_cycles);
    format_cycles(rt, sizeof(rt), r->t_roundtrip_cycles);
    format_result(bret, sizeof(bret), r->baseline_ret_code);
    format_result(f1ret, sizeof(f1ret), r->first_fix_ret_code);
    format_result(fret, sizeof(fret), r->fix_ret_code);
    format_result(uret, sizeof(uret), r->unfix_ret_code);

    console_printf("\r\n[scheme] %s\r\n", name);
    console_puts("=== Benchmark Stages ===\r\n");
    console_printf("scheme=%s target=%s available=%s\r\n",
                   name, cve_target_name(cve_target_get_current()), yes_no(r->available));
    console_printf("baseline       ret=%-16s ok=%-3s cycles=%s\r\n", bret, yes_no(r->baseline_ok), base);
    console_printf("patch_only     ok=%-3s cycles=%s\r\n", yes_no(r->pure_patch_ok), patch);
    console_printf("apply+fix1     ret=%-16s ok=%-3s cycles=%s\r\n", f1ret, yes_no(r->first_fix_ok), fix1);
    console_printf("patched_%03lu    ret=%-16s ok=%-3s total=%s steady=%s\r\n",
                   (unsigned long)BENCHMARK_PATCHED_CALLS, fret, yes_no(r->fix_ok), fix100, steady);
    console_printf("unpatch+call   ret=%-16s ok=%-3s cycles=%s roundtrip=%s\r\n",
                   uret, yes_no(r->unfix_ok), unfix, rt);
}

static void print_compare_row(const char *label, uint32_t morph_v, uint32_t erase_v)
{
    char mb[16], eb[16], db[24];
    format_cycles(mb, sizeof(mb), morph_v);
    format_cycles(eb, sizeof(eb), erase_v);
    if (morph_v != 0xFFFFFFFFu && erase_v != 0xFFFFFFFFu) {
        if (erase_v >= morph_v)
            (void)snprintf(db, sizeof(db), "+%lu", (unsigned long)(erase_v - morph_v));
        else
            (void)snprintf(db, sizeof(db), "-%lu", (unsigned long)(morph_v - erase_v));
    } else {
        (void)snprintf(db, sizeof(db), "N/A");
    }
    console_printf("%-14s morph=%-15s erase-rewr=%-12s delta=%s\r\n", label, mb, eb, db);
}

void benchmark_compare_run_and_print(void)
{
    static const cmp_scheme_t morph_scheme = {
        .name = "MorphPatch (erase-free, 1->0 only)",
        .call = patch_call,
        .apply = patch_apply,
        .unapply = patch_unapply,
        .demo_can_run = patch_demo_can_run,
    };
    static const cmp_scheme_t erase_scheme = {
        .name = "EraseRewritePatch (page erase + rewrite)",
        .call = erase_patch_call,
        .apply = erase_patch_apply,
        .unapply = erase_patch_unapply,
        .demo_can_run = erase_patch_demo_can_run,
    };

    console_printf("\r\n=== Benchmark Compare [%s] ===\r\n",
                   cve_target_name(cve_target_get_current()));

    cmp_result_t erase_r = run_one_scheme(&erase_scheme);
    cmp_result_t morph_r = run_one_scheme(&morph_scheme);

    print_scheme_result(morph_scheme.name, &morph_r);
    print_scheme_result(erase_scheme.name, &erase_r);

    console_puts("\r\n=== Compare Summary ===\r\n");
    console_puts("[delta] erase-rewrite - morph\r\n");
    print_compare_row("baseline",    morph_r.t_base_cycles,       erase_r.t_base_cycles);
    print_compare_row("patch_only",  morph_r.t_patch_cycles,      erase_r.t_patch_cycles);
    print_compare_row("apply+fix1",  morph_r.t_fix_first_cycles,  erase_r.t_fix_first_cycles);
    print_compare_row("patched_100", morph_r.t_fix_cycles,        erase_r.t_fix_cycles);
    print_compare_row("steady",      morph_r.t_steady_cycles,     erase_r.t_steady_cycles);
    print_compare_row("unpatch+call",morph_r.t_unfix_cycles,      erase_r.t_unfix_cycles);
    print_compare_row("roundtrip",   morph_r.t_roundtrip_cycles,  erase_r.t_roundtrip_cycles);
}
