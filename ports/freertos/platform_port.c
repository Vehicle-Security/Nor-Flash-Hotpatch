/*
 * platform_port.c — FreeRTOS platform port for ClearBitPatch.
 *
 * Runs two tasks: a victim task that periodically calls the patch slot
 * (simulating real workload), and a control task that handles the RTT
 * console and patch management.  A mutex serialises flash writes so
 * the victim is suspended while the patch region is being programmed.
 */
#include "ports/common/platform_port.h"

#include <stdbool.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "SEGGER_RTT.h"

#include "core/app/clearbit_app.h"
#include "freertos_port.h"

int SEGGER_RTT_vprintf(unsigned BufferIndex, const char *sFormat, va_list *pParamList);

enum {
    FREERTOS_CONTROL_TASK_STACK_WORDS = 1024u,
    FREERTOS_VICTIM_TASK_STACK_WORDS = 1024u,
    FREERTOS_CONTROL_TASK_PRIORITY = 2u,
    FREERTOS_VICTIM_TASK_PRIORITY = 3u,
};

typedef struct {
    SemaphoreHandle_t patch_mutex;
    SemaphoreHandle_t call_mutex;
    TaskHandle_t control_task;
    TaskHandle_t victim_task;
    volatile TaskHandle_t requester_task;
    volatile platform_patch_entry_fn_t pending_entry;
    volatile int pending_result;
} freertos_port_state_t;

static freertos_port_state_t g_freertos_port = {0};

static bool freertos_scheduler_running(void) {
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

static int read_key_blocking(void) {
    int key = -1;

    do {
        key = SEGGER_RTT_GetKey();
        if (key < 0 && freertos_scheduler_running()) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
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
            digit = (uint32_t)(text[i] - 'a' + 10);
        } else if (base == 16u && text[i] >= 'A' && text[i] <= 'F') {
            digit = (uint32_t)(text[i] - 'A' + 10);
        } else {
            return false;
        }

        parsed = parsed * base + digit;
    }

    *value = parsed;
    return true;
}

static void freertos_control_task(void *argument) {
    (void)argument;

    clearbit_app_boot();

    for (;;) {
        clearbit_app_process_once();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void freertos_victim_task(void *argument) {
    (void)argument;

    for (;;) {
        TaskHandle_t requester = NULL;
        platform_patch_entry_fn_t entry = NULL;
        int result = 0;

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        requester = (TaskHandle_t)g_freertos_port.requester_task;
        entry = (platform_patch_entry_fn_t)g_freertos_port.pending_entry;
        if (entry == NULL) {
            if (requester != NULL) {
                xTaskNotifyGive(requester);
            }
            continue;
        }

        if (g_freertos_port.patch_mutex != NULL) {
            (void)xSemaphoreTake(g_freertos_port.patch_mutex, portMAX_DELAY);
        }

        result = entry();
        g_freertos_port.pending_result = result;

        if (g_freertos_port.patch_mutex != NULL) {
            (void)xSemaphoreGive(g_freertos_port.patch_mutex);
        }

        if (requester != NULL) {
            xTaskNotifyGive(requester);
        }
    }
}

bool freertos_port_init(void) {
    g_freertos_port.patch_mutex = xSemaphoreCreateMutex();
    g_freertos_port.call_mutex = xSemaphoreCreateMutex();

    return g_freertos_port.patch_mutex != NULL
        && g_freertos_port.call_mutex != NULL;
}

bool freertos_port_start(void) {
    BaseType_t ok = pdPASS;

    ok = xTaskCreate(
        freertos_victim_task,
        "cb_victim",
        FREERTOS_VICTIM_TASK_STACK_WORDS,
        NULL,
        FREERTOS_VICTIM_TASK_PRIORITY,
        &g_freertos_port.victim_task);
    if (ok != pdPASS) {
        return false;
    }

    ok = xTaskCreate(
        freertos_control_task,
        "cb_shell",
        FREERTOS_CONTROL_TASK_STACK_WORDS,
        NULL,
        FREERTOS_CONTROL_TASK_PRIORITY,
        &g_freertos_port.control_task);
    if (ok != pdPASS) {
        return false;
    }

    return true;
}

void platform_console_init(void) {
    SEGGER_RTT_Init();
    SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_NO_BLOCK_TRIM);
    SEGGER_RTT_WriteString(0, "\r\nRTT console ready. (ClearBitPatch Demo)\r\n");
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
    platform_console_write("rtt> ");
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

int platform_patch_call(platform_patch_entry_fn_t entry) {
    int result = 0;
    TaskHandle_t self = NULL;

    if (entry == NULL) {
        return 0;
    }

    if (!freertos_scheduler_running() || g_freertos_port.victim_task == NULL) {
        return entry();
    }

    self = xTaskGetCurrentTaskHandle();
    if (self == g_freertos_port.victim_task) {
        if (g_freertos_port.patch_mutex != NULL) {
            (void)xSemaphoreTake(g_freertos_port.patch_mutex, portMAX_DELAY);
        }
        result = entry();
        if (g_freertos_port.patch_mutex != NULL) {
            (void)xSemaphoreGive(g_freertos_port.patch_mutex);
        }
        return result;
    }

    if (g_freertos_port.call_mutex == NULL) {
        return entry();
    }

    (void)xSemaphoreTake(g_freertos_port.call_mutex, portMAX_DELAY);
    (void)ulTaskNotifyTake(pdTRUE, 0u);

    g_freertos_port.requester_task = self;
    g_freertos_port.pending_entry = entry;
    xTaskNotifyGive(g_freertos_port.victim_task);

    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    result = g_freertos_port.pending_result;

    g_freertos_port.pending_entry = NULL;
    g_freertos_port.requester_task = NULL;
    (void)xSemaphoreGive(g_freertos_port.call_mutex);

    return result;
}

void platform_lock_patch_region(void) {
    if (g_freertos_port.patch_mutex == NULL || !freertos_scheduler_running()) {
        return;
    }

    (void)xSemaphoreTake(g_freertos_port.patch_mutex, portMAX_DELAY);
}

void platform_unlock_patch_region(void) {
    if (g_freertos_port.patch_mutex == NULL || !freertos_scheduler_running()) {
        return;
    }

    (void)xSemaphoreGive(g_freertos_port.patch_mutex);
}

void platform_victim_suspend(void) {
    if (g_freertos_port.victim_task == NULL || !freertos_scheduler_running()) {
        return;
    }

    if (xTaskGetCurrentTaskHandle() == g_freertos_port.victim_task) {
        return;
    }

    vTaskSuspend(g_freertos_port.victim_task);
}

void platform_victim_resume(void) {
    if (g_freertos_port.victim_task == NULL || !freertos_scheduler_running()) {
        return;
    }

    if (xTaskGetCurrentTaskHandle() == g_freertos_port.victim_task) {
        return;
    }

    vTaskResume(g_freertos_port.victim_task);
}
