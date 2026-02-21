#include "benchmark.h"
#include "base_module.h"
#include "method_a.h"
#include "method_b.h"

static bool is_exec_safe_target(uint16_t hw)
{
    if (hw < THUMB_SHIFT_ADD_LIMIT) {
        return true;
    }

    if ((hw & THUMB_DATA_PROC_MASK) == THUMB_DATA_PROC_BASE) {
        return true;
    }

    return false;
}

static void print_ratio3(uint32_t num, uint32_t den)
{
    if (den == 0u) {
        rtt_puts("inf");
        return;
    }

    uint64_t r1000 = ((uint64_t)num * 1000ull + (uint64_t)den / 2ull) / (uint64_t)den;
    rtt_put_u32((uint32_t)(r1000 / 1000ull));
    rtt_putc('.');

    uint32_t frac = (uint32_t)(r1000 % 1000ull);
    rtt_putc((char)('0' + (frac / 100u)));
    rtt_putc((char)('0' + ((frac / 10u) % 10u)));
    rtt_putc((char)('0' + (frac % 10u)));
}

static bool ensure_start_state(uint32_t page_addr)
{
    method_a_disable_patch();
    method_a_reset_status();

    if (slot_read_hw(page_addr) != (uint16_t)BASE_UDF_HW) {
        if (!slot_set_udf_with_erase(page_addr)) {
            return false;
        }
    }

    return true;
}

void run_benchmark(uint32_t page_addr)
{
    static const uint32_t kLoopCounts[] = {
        1u, 10u, 100u, 1000u, 10000u, 50000u, 78000u,78500u, 79000u, 79500u, 80000u, 100000u
    };
    const uint32_t loop_count_num = (uint32_t)(sizeof(kLoopCounts) / sizeof(kLoopCounts[0]));

    rtt_puts("\r\n=== nRF52840 Hotfix Benchmark (Modular) ===\r\n");
    rtt_puts("Bench Page: ");
    rtt_put_hex32(page_addr);
    rtt_puts("\r\n");
    rtt_puts("Base UDF : ");
    rtt_put_hex16((uint16_t)BASE_UDF_HW);
    rtt_puts("\r\n\r\n");

    if (!ensure_start_state(page_addr)) {
        rtt_puts("ERR: cannot init slot to UDF\r\n");
        return;
    }

    uint32_t ok_rows = 0u;
    uint32_t fail_rows = 0u;
    uint32_t skipped = 0u;
    uint32_t tested_hw = 0u;

    for (uint32_t candidate = 0u; candidate <= 0xFFFFu; ++candidate) {
        uint16_t hw = (uint16_t)candidate;

        /*
         * 仅保留能从 BASE_UDF_HW 通过 1->0 清位得到的半字。
         * 这用于评估“只清位可达”的热修复策略。
         */
        if ((hw & (uint16_t)(~(uint16_t)BASE_UDF_HW)) != 0u) {
            continue;
        }
        if (hw == (uint16_t)BASE_UDF_HW) {
            continue;
        }

        if (!is_exec_safe_target(hw)) {
            skipped++;
            continue;
        }
        tested_hw++;

        for (uint32_t i = 0u; i < loop_count_num; ++i) {
            uint32_t n = kLoopCounts[i];

            if (!ensure_start_state(page_addr)) {
                fail_rows++;
                continue;
            }

            uint32_t a_cyc = 0u;
            uint32_t b_cyc = 0u;
            bool ok_a = measure_method_A(page_addr, hw, n, &a_cyc);
            bool ok_b = measure_method_B(page_addr, hw, n, &b_cyc);

            if (!ok_a || !ok_b) {
                fail_rows++;
                /* 尽量恢复，避免影响下一组测试 */
                (void)slot_set_udf_with_erase(page_addr);
                method_a_disable_patch();
                continue;
            }

            ok_rows++;
            rtt_puts("hw=");
            rtt_put_hex16(hw);
            rtt_puts(" N=");
            rtt_put_u32(n);
            rtt_puts("  A_cyc=");
            rtt_put_u32(a_cyc);
            rtt_puts("  B_cyc=");
            rtt_put_u32(b_cyc);
            rtt_puts("  ratio(B/A)=");
            print_ratio3(b_cyc, a_cyc);
            rtt_puts("\r\n");
        }
    }

    rtt_puts("\r\n[SUMMARY] OK=");
    rtt_put_u32(ok_rows);
    rtt_puts(" FAIL=");
    rtt_put_u32(fail_rows);
    rtt_puts(" SAFE_HW=");
    rtt_put_u32(tested_hw);
    rtt_puts(" SKIPPED=");
    rtt_put_u32(skipped);
    rtt_puts("\r\n");
}
