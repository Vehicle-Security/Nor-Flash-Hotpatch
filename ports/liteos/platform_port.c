#include "ports/common/platform_port.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "SEGGER_RTT.h"
#include "core/app/morph_app.h"
#include "liteos_port.h"

int SEGGER_RTT_vprintf(unsigned BufferIndex, const char *sFormat, va_list *pParamList);

static int read_key_blocking(void) {
    int key = -1;

    do {
        key = SEGGER_RTT_GetKey();
    } while (key < 0);

    return key;
}

static bool parse_u32_string(const char *text, uint32_t *value) {
    uint32_t parsed = 0u;
    uint32_t base = 10u;
    size_t start = 0u;

    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16u;
        start = 2u;
        if (text[start] == '\0') {
            return false;
        }
    }

    for (size_t i = start; text[i] != '\0'; ++i) {
        uint32_t digit = 0u;

        if (text[i] >= '0' && text[i] <= '9') {
            digit = (uint32_t)(text[i] - '0');
        } else if (base == 16u && text[i] >= 'a' && text[i] <= 'f') {
            digit = (uint32_t)(text[i] - 'a' + 10u);
        } else if (base == 16u && text[i] >= 'A' && text[i] <= 'F') {
            digit = (uint32_t)(text[i] - 'A' + 10u);
        } else {
            return false;
        }

        parsed = (parsed * base) + digit;
    }

    *value = parsed;
    return true;
}

#ifdef MORPH_LITEOS_NATIVE

#include <string.h>

#include "los_base.h"
#include "los_mux.h"
#include "los_sem.h"
#include "los_task.h"
#include "los_tick.h"

enum {
    LITEOS_CONTROL_TASK_STACK_BYTES = 4096u,
    LITEOS_VICTIM_TASK_STACK_BYTES = 4096u,
    LITEOS_CONTROL_TASK_PRIORITY = 10u,
    LITEOS_VICTIM_TASK_PRIORITY = 9u,
};

typedef struct {
    UINT32 patch_mux;
    UINT32 call_mux;
    UINT32 victim_kick_sem;
    UINT32 request_done_sem;
    UINT32 control_task;
    UINT32 victim_task;
    platform_patch_entry_fn_t pending_entry;
    int pending_result;
    bool initialized;
    bool started;
} liteos_port_state_t;

static liteos_port_state_t g_liteos_port = {0};

static UINT32 delay_ticks_1ms(void) {
    UINT32 ticks = LOS_MS2Tick(1u);

    return (ticks == 0u) ? 1u : ticks;
}

static void sleep_1ms(void) {
    (void)LOS_TaskDelay(delay_ticks_1ms());
}

static bool wait_sem_forever(UINT32 sem_handle) {
    return LOS_SemPend(sem_handle, LOS_WAIT_FOREVER) == LOS_OK;
}

static void drain_sem(UINT32 sem_handle) {
    while (LOS_SemPend(sem_handle, LOS_NO_WAIT) == LOS_OK) {
    }
}

static VOID *liteos_control_task(void *argument) {
    (void)argument;

    morph_app_boot();

    for (;;) {
        morph_app_process_once();
        sleep_1ms();
    }
}

static VOID *liteos_victim_task(void *argument) {
    (void)argument;

    for (;;) {
        platform_patch_entry_fn_t entry = NULL;
        int result = 0;

        (void)wait_sem_forever(g_liteos_port.victim_kick_sem);
        entry = g_liteos_port.pending_entry;
        if (entry == NULL) {
            (void)LOS_SemPost(g_liteos_port.request_done_sem);
            continue;
        }

        (void)LOS_MuxPend(g_liteos_port.patch_mux, LOS_WAIT_FOREVER);
        result = entry();
        g_liteos_port.pending_result = result;
        (void)LOS_MuxPost(g_liteos_port.patch_mux);

        (void)LOS_SemPost(g_liteos_port.request_done_sem);
    }
}

static bool create_task(UINT32 *task_id,
                        TSK_ENTRY_FUNC entry,
                        CHAR *name,
                        UINT16 priority,
                        UINT32 stack_size) {
    TSK_INIT_PARAM_S init_param;

    if (task_id == NULL || entry == NULL || name == NULL) {
        return false;
    }

    (void)memset(&init_param, 0, sizeof(init_param));
    init_param.pfnTaskEntry = entry;
    init_param.usTaskPrio = priority;
#ifdef LOSCFG_OBSOLETE_API
    init_param.auwArgs[0] = 0;
    init_param.auwArgs[1] = 0;
    init_param.auwArgs[2] = 0;
    init_param.auwArgs[3] = 0;
#else
    init_param.pArgs = NULL;
#endif
    init_param.uwStackSize = stack_size;
    init_param.pcName = name;
    init_param.uwResved = LOS_TASK_STATUS_DETACHED;

    return LOS_TaskCreate(task_id, &init_param) == LOS_OK;
}

bool liteos_port_init(void) {
    if (g_liteos_port.initialized) {
        return true;
    }

    if (LOS_MuxCreate(&g_liteos_port.patch_mux) != LOS_OK) {
        return false;
    }

    if (LOS_MuxCreate(&g_liteos_port.call_mux) != LOS_OK) {
        (void)LOS_MuxDelete(g_liteos_port.patch_mux);
        return false;
    }

    if (LOS_BinarySemCreate(0, &g_liteos_port.victim_kick_sem) != LOS_OK) {
        (void)LOS_MuxDelete(g_liteos_port.call_mux);
        (void)LOS_MuxDelete(g_liteos_port.patch_mux);
        return false;
    }

    if (LOS_BinarySemCreate(0, &g_liteos_port.request_done_sem) != LOS_OK) {
        (void)LOS_SemDelete(g_liteos_port.victim_kick_sem);
        (void)LOS_MuxDelete(g_liteos_port.call_mux);
        (void)LOS_MuxDelete(g_liteos_port.patch_mux);
        return false;
    }

    g_liteos_port.pending_entry = NULL;
    g_liteos_port.pending_result = 0;
    g_liteos_port.initialized = true;
    g_liteos_port.started = false;
    return true;
}

bool liteos_port_start(void) {
    static CHAR victim_name[] = "cbp_victim";
    static CHAR control_name[] = "cbp_control";

    if (!g_liteos_port.initialized) {
        return false;
    }

    if (g_liteos_port.started) {
        return true;
    }

    if (!create_task(&g_liteos_port.victim_task,
                     liteos_victim_task,
                     victim_name,
                     (UINT16)LITEOS_VICTIM_TASK_PRIORITY,
                     (UINT32)LITEOS_VICTIM_TASK_STACK_BYTES)) {
        return false;
    }

    if (!create_task(&g_liteos_port.control_task,
                     liteos_control_task,
                     control_name,
                     (UINT16)LITEOS_CONTROL_TASK_PRIORITY,
                     (UINT32)LITEOS_CONTROL_TASK_STACK_BYTES)) {
        (void)LOS_TaskDelete(g_liteos_port.victim_task);
        return false;
    }

    g_liteos_port.started = true;
    return true;
}

int platform_patch_call(platform_patch_entry_fn_t entry) {
    int result = 0;

    if (entry == NULL) {
        return 0;
    }

    if (!g_liteos_port.started) {
        return entry();
    }

    if (LOS_CurTaskIDGet() == g_liteos_port.victim_task) {
        (void)LOS_MuxPend(g_liteos_port.patch_mux, LOS_WAIT_FOREVER);
        result = entry();
        (void)LOS_MuxPost(g_liteos_port.patch_mux);
        return result;
    }

    if (LOS_MuxPend(g_liteos_port.call_mux, LOS_WAIT_FOREVER) != LOS_OK) {
        return entry();
    }

    drain_sem(g_liteos_port.request_done_sem);
    g_liteos_port.pending_entry = entry;

    if (LOS_SemPost(g_liteos_port.victim_kick_sem) != LOS_OK) {
        g_liteos_port.pending_entry = NULL;
        (void)LOS_MuxPost(g_liteos_port.call_mux);
        return entry();
    }

    if (!wait_sem_forever(g_liteos_port.request_done_sem)) {
        g_liteos_port.pending_entry = NULL;
        (void)LOS_MuxPost(g_liteos_port.call_mux);
        return entry();
    }

    result = g_liteos_port.pending_result;
    g_liteos_port.pending_entry = NULL;
    (void)LOS_MuxPost(g_liteos_port.call_mux);
    return result;
}

void platform_lock_patch_region(void) {
    if (!g_liteos_port.started) {
        return;
    }

    (void)LOS_MuxPend(g_liteos_port.patch_mux, LOS_WAIT_FOREVER);
}

void platform_unlock_patch_region(void) {
    if (!g_liteos_port.started) {
        return;
    }

    (void)LOS_MuxPost(g_liteos_port.patch_mux);
}

void platform_victim_suspend(void) {
    if (!g_liteos_port.started) {
        return;
    }

    if (LOS_CurTaskIDGet() == g_liteos_port.victim_task) {
        return;
    }

    (void)LOS_TaskSuspend(g_liteos_port.victim_task);
}

void platform_victim_resume(void) {
    if (!g_liteos_port.started) {
        return;
    }

    if (LOS_CurTaskIDGet() == g_liteos_port.victim_task) {
        return;
    }

    (void)LOS_TaskResume(g_liteos_port.victim_task);
}

#else

bool liteos_port_init(void) {
    return true;
}

bool liteos_port_start(void) {
    return true;
}

int platform_patch_call(platform_patch_entry_fn_t entry) {
    if (entry == NULL) {
        return 0;
    }

    return entry();
}

void platform_lock_patch_region(void) {
}

void platform_unlock_patch_region(void) {
}

void platform_victim_suspend(void) {
}

void platform_victim_resume(void) {
}

#endif

void platform_console_init(void) {
    SEGGER_RTT_Init();
    SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_NO_BLOCK_TRIM);
    SEGGER_RTT_WriteString(0, "\r\nLiteOS console ready. (MorphPatch Demo)\r\n");
}

void platform_console_write(const char *text) {
    if (text == NULL) {
        return;
    }

    SEGGER_RTT_WriteString(0, text);
}

int platform_console_vprintf(const char *format, va_list *args) {
    if (format == NULL || args == NULL) {
        return 0;
    }

    return SEGGER_RTT_vprintf(0, format, args);
}

void platform_command_prompt(void) {
    platform_console_write("liteos> ");
}

bool platform_command_poll_line(char *buf, size_t buf_size, size_t *len) {
    int key = 0;

    if (buf == NULL || len == NULL || buf_size == 0u) {
        return false;
    }

    key = SEGGER_RTT_GetKey();
    if (key < 0) {
        return false;
    }

    if (key == '\r' || key == '\n') {
        platform_console_write("\r\n");
        buf[*len] = '\0';
        return true;
    }

    if ((key == '\b' || key == 0x7F) && *len > 0u) {
        (*len)--;
        platform_console_write("\b \b");
        return false;
    }

    if (key >= 0x20 && key <= 0x7E && *len < (buf_size - 1u)) {
        buf[*len] = (char)key;
        (*len)++;
        SEGGER_RTT_PutChar(0, (char)key);
    }

    return false;
}

bool platform_command_prompt_u32(const char *prompt, uint32_t *out) {
    char buf[24];
    size_t len = 0u;

    if (prompt == NULL || out == NULL) {
        return false;
    }

    platform_console_write(prompt);

    while (len < (sizeof(buf) - 1u)) {
        int key = read_key_blocking();

        if (key == '\r' || key == '\n') {
            if (len == 0u) {
                continue;
            }
            break;
        }

        if (key == '\b' || key == 0x7F) {
            if (len > 0u) {
                len--;
                platform_console_write("\b \b");
            }
            continue;
        }

        buf[len] = (char)key;
        len++;
        SEGGER_RTT_PutChar(0, (char)key);
    }

    buf[len] = '\0';
    platform_console_write("\r\n");
    return parse_u32_string(buf, out);
}
