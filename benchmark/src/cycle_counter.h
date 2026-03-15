#ifndef CYCLE_COUNTER_H
#define CYCLE_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

bool cycle_counter_init(void);
bool cycle_counter_reset(void);
uint32_t cycle_counter_read(void);

#endif
