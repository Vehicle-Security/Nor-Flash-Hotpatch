#ifndef APP_MODE_H
#define APP_MODE_H

#include <stdbool.h>

typedef enum {
    APP_EXEC_MODE_INTERACTIVE = 0,
    APP_EXEC_MODE_DEMO = 1,
    APP_EXEC_MODE_BENCHMARK = 2,
} app_exec_mode_t;

void app_set_exec_mode(app_exec_mode_t mode);
app_exec_mode_t app_get_exec_mode(void);
bool app_exec_mode_is_verbose(void);

#endif
