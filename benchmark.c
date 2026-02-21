#include "benchmark.h"
#include "base_module.h"
#include "method_a.h"
#include "method_b.h"

static bool is_exec_safe_target(uint16_t hw) {
    if (hw < THUMB_SHIFT_ADD_LIMIT) return true;
    if ((hw & THUMB_DATA_PROC_MASK) == THUMB_DATA_PROC_BASE) return true;
    return false;
}

static void print_ratio3(uint32_t num, uint32_t den) {
    if (den == 0u) { rtt_puts("inf"); return; }
    uint64_t r1000 = ((uint64_t)num * 1000ull + (uint64_t)den / 2ull) / (uint64_t)den;
    rtt_put_u32((uint32_t)(r1000 / 1000ull));
    rtt_putc('.');
    uint32_t frac = (uint32_t)(r1000 % 1000ull);
    rtt_putc((char)('0' + (frac / 100u)));
    rtt_putc((char)('0' + ((frac / 10u) % 10u)));
    rtt_putc((char)('0' + (frac % 10u)));
}

static bool ensure_start_state(uint32_t page_addr) {
    method_a_disable_patch();
    method_a_reset_status();

    if (slot_read_hw(page_addr) != (uint16_t)BASE_UDF_HW) {
        if (!slot_set_udf_with_erase(page_addr)) return false;
    }
    return true;
}

void run_benchmark(uint32_t page_addr) {
    static const uint32_t Ns[] = { 1, 10, 100, 1000, 10000, 50000, 60000, 64000, 65000, 66000, 100000 };

    rtt_puts("\r\n=== nRF52840 Hotfix Benchmark (Modular) ===\r\n");
    rtt_puts("Bench Page: "); rtt_put_hex32(page_addr); rtt_puts("\r\n");
    rtt_puts("Base UDF : "); rtt_put_hex16((uint16_t)BASE_UDF_HW); rtt_puts("\r\n\r\n");

    if (!ensure_start_state(page_addr)) {
        rtt_puts("ERR: cannot init slot to UDF\r\n");
        return;
    }

    uint32_t ok_rows = 0, fail_rows = 0, skipped = 0, tested_hw = 0;

    for (uint32_t cand = 0; cand <= 0xFFFFu; cand++) {
        uint16_t hw = (uint16_t)cand;

        /* If you are exploring "bit-clear-only patchability" from BASE_UDF_HW, keep this filter.
           It keeps only halfwords obtainable by clearing 1->0 from BASE_UDF_HW. */
        if ((hw & (uint16_t)(~(uint16_t)BASE_UDF_HW)) != 0u) continue;
        if (hw == (uint16_t)BASE_UDF_HW) continue;

        if (!is_exec_safe_target(hw)) { skipped++; continue; }
        tested_hw++;

        for (uint32_t ni = 0; ni < (sizeof(Ns) / sizeof(Ns[0])); ni++) {
            uint32_t N = Ns[ni];

            if (!ensure_start_state(page_addr)) { fail_rows++; continue; }

            uint32_t a_cyc = 0, b_cyc = 0;
            bool okA = measure_method_A(page_addr, hw, N, &a_cyc);
            bool okB = measure_method_B(page_addr, hw, N, &b_cyc);

            if (!okA || !okB) {
                fail_rows++;
                /* Best-effort recovery for next run */
                (void)slot_set_udf_with_erase(page_addr);
                method_a_disable_patch();
                continue;
            }

            ok_rows++;
            rtt_puts("hw="); rtt_put_hex16(hw);
            rtt_puts(" N="); rtt_put_u32(N);
            rtt_puts("  A_cyc="); rtt_put_u32(a_cyc);
            rtt_puts("  B_cyc="); rtt_put_u32(b_cyc);
            rtt_puts("  ratio(B/A)="); print_ratio3(b_cyc, a_cyc);
            rtt_puts("\r\n");
        }
    }

    rtt_puts("\r\n[SUMMARY] OK="); rtt_put_u32(ok_rows);
    rtt_puts(" FAIL="); rtt_put_u32(fail_rows);
    rtt_puts(" SAFE_HW="); rtt_put_u32(tested_hw);
    rtt_puts(" SKIPPED="); rtt_put_u32(skipped);
    rtt_puts("\r\n");
}
