#ifndef METHOD_A_H
#define METHOD_A_H

#include "base_module.h"

/* A 方案（异常驱动动态热修复）的状态 */
typedef enum {
    HOTFIX_STATUS_NONE = 0,
    HOTFIX_STATUS_INVALID_FAULT = 1,
    HOTFIX_STATUS_PATCH_OK = 2
} MethodAStatus_t;

/* 安装 HardFault/UsageFault 拦截并绑定目标 slot 地址 */
void method_a_init(uint32_t slot_page_addr);

/* 状态查询辅助接口 */
uint32_t method_a_get_fault_count(void);
MethodAStatus_t method_a_get_status(void);
void method_a_reset_status(void);
void method_a_disable_patch(void);

/* 测量 A 方案耗时，返回是否成功，周期数写入 out_cycles */
bool measure_method_A(uint32_t page_addr, uint16_t hw, uint32_t N, uint32_t *out_cycles);

#endif /* METHOD_A_H */
