#include "nrf.h"
#include "base_module.h"
#include "method_a.h"
#include "benchmark.h"

int main(void) {
    SystemCoreClockUpdate();
    dwt_init();

    uint32_t page = bench_page_addr();

    /* Install interception and bind target slot address. */
    method_a_init(page);

    run_benchmark(page);

    rtt_puts("\r\n[END] halt\r\n");
    while (1) { __WFE(); }
}
