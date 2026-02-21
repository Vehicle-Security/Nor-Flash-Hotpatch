#include "method_b.h"
#include "method_a.h"

/*
 * B 方案前提：
 * 1. slot 初始指令是 UDF（BASE_UDF_HW）。
 * 2. 已安装异常拦截（用于统计 fault）。
 * 3. 方案执行期间保持 patch 关闭（N 次循环中应为 0 fault）。
 */
bool measure_method_B(uint32_t page_addr, uint16_t hw, uint32_t N, uint32_t *out_cycles)
{
    if (out_cycles == NULL) {
        return false;
    }

    method_a_disable_patch();

    /* 预检查：slot 为 UDF 时应只触发一次 INVALID_FAULT */
    uint32_t pre_f0 = method_a_get_fault_count();
    method_a_reset_status();
    (void)execute_target_slot(page_addr);

    if (method_a_get_status() != HOTFIX_STATUS_INVALID_FAULT || method_a_get_fault_count() != (pre_f0 + 1u)) {
        return false;
    }

    uint32_t t0 = cyc_now();

    /* 1) 擦除并写入目标半字 */
    if (!slot_set_hw_with_erase(page_addr, hw)) {
        return false;
    }

    /* 2) 执行 N 次，不应出现 fault */
    uint32_t f0 = method_a_get_fault_count();
    for (uint32_t i = 0u; i < N; ++i) {
        method_a_reset_status();
        (void)execute_target_slot(page_addr);
    }
    uint32_t f1 = method_a_get_fault_count();

    /* 3) 恢复为 UDF（擦除并写回） */
    if (!slot_set_udf_with_erase(page_addr)) {
        return false;
    }

    *out_cycles = (cyc_now() - t0);

    /* N 次循环里不应该新增 fault */
    if ((f1 - f0) != 0u) {
        return false;
    }

    return true;
}
