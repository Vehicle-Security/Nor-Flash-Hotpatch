#include <stdbool.h>

#include "core/app/clearbit_app.h"

typedef struct {
    void *control_thread;
    void *victim_thread;
    void *patch_lock;
} zephyr_port_state_t;

static zephyr_port_state_t g_zephyr_port = {0};

static void zephyr_control_thread_entry(void *arg1, void *arg2, void *arg3) {
    (void)arg1;
    (void)arg2;
    (void)arg3;

    clearbit_app_boot();

    for (;;) {
        clearbit_app_process_once();
        /* TODO: move to Zephyr shell callbacks or another chosen command backend. */
    }
}

static void zephyr_victim_thread_entry(void *arg1, void *arg2, void *arg3) {
    (void)arg1;
    (void)arg2;
    (void)arg3;

    for (;;) {
        /*
         * TODO: host the victim workload in a normal Zephyr thread.
         * Keep ClearBitPatch as an in-place function redirect with no payload thread.
         */
    }
}

static bool zephyr_port_create_primitives(void) {
    /*
     * TODO:
     * - create a k_mutex or other small synchronization primitive for patching
     * - create one control thread and one victim thread
     * - register shell glue once the Zephyr command path is chosen
     */
    return false;
}

int main(void) {
    (void)g_zephyr_port;
    (void)zephyr_control_thread_entry;
    (void)zephyr_victim_thread_entry;

    return zephyr_port_create_primitives() ? 0 : -1;
}
