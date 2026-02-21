#include "nrf.h"
#include "base_module.h"
#include "method_a.h"
#include "benchmark.h"

int main(void)
{
    /* 初始化系统时钟和周期计数器 */
    SystemCoreClockUpdate();
    dwt_init();

    uint32_t page = bench_page_addr();

    /* 安装异常拦截并绑定目标 slot 地址 */
    method_a_init(page);

    /* 执行基准测试 */
    run_benchmark(page);

    rtt_puts("\r\n[END] halt\r\n");
    while (1) {
        __WFE();
    }
}
