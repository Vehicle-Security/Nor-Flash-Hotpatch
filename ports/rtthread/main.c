#include <stdbool.h>

#include "core/app/clearbit_app.h"

typedef struct {
    void *control_thread;
    void *victim_thread;
    void *patch_mutex;
} rtthread_port_state_t;

static rtthread_port_state_t g_rtthread_port = {0};

static void rtthread_control_thread_entry(void *parameter) {
    (void)parameter;

    clearbit_app_boot();

    for (;;) {
        clearbit_app_process_once();
        /* TODO: swap this polling loop for the RT-Thread shell/backend hook. */
    }
}

static void rtthread_victim_thread_entry(void *parameter) {
    (void)parameter;

    for (;;) {
        /*
         * TODO: run the victim workload in its own RT-Thread thread.
         * Do not introduce a thread for the patch payload or patched function body.
         */
    }
}

static bool rtthread_port_create_primitives(void) {
    /*
     * TODO:
     * - create an RT-Thread mutex or scheduler lock path for patch apply/unapply
     * - create one control thread and one victim thread
     * - hook this into rt_application_init()/MSH as appropriate
     */
    return false;
}

int main(void) {
    (void)g_rtthread_port;
    (void)rtthread_control_thread_entry;
    (void)rtthread_victim_thread_entry;

    return rtthread_port_create_primitives() ? 0 : -1;
}
