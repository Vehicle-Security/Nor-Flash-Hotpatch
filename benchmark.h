#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>

/* 运行完整基准测试，page_addr 指向用于执行/擦写的 flash 页面 */
void run_benchmark(uint32_t page_addr);

#endif /* BENCHMARK_H */
