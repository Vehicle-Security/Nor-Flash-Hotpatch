#include "app_mode.h"

static app_exec_mode_t g_exec_mode = APP_EXEC_MODE_INTERACTIVE;

void app_set_exec_mode(app_exec_mode_t mode) {
    g_exec_mode = mode;
}

app_exec_mode_t app_get_exec_mode(void) {
    return g_exec_mode;
}

bool app_exec_mode_is_verbose(void) {
    return g_exec_mode != APP_EXEC_MODE_BENCHMARK;
}
