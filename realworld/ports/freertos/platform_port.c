#include "ports/common/platform_port.h"

#include <stdbool.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "SEGGER_RTT.h"
#include "bsp.h"

#include "core/patch/patch_control.h"
#include "realworld/src/led_demo.h"
#include "freertos_port.h"

int SEGGER_RTT_vprintf(unsigned BufferIndex, const char *sFormat, va_list *pParamList);

enum {
    FREERTOS_VICTIM_TASK_STACK_WORDS = 1024u,
    FREERTOS_PATCH_TASK_STACK_WORDS = 1024u,
    FREERTOS_PATCH_TASK_PRIORITY = 2u,
    FREERTOS_VICTIM_TASK_PRIORITY = 3u,
    FREERTOS_BLINK_INTERVAL_MS = 250u,
    FREERTOS_BUTTON_POLL_MS = 20u,
};

typedef struct {
    SemaphoreHandle_t patch_mutex;
    TaskHandle_t victim_task;
    TaskHandle_t patch_task;
} freertos_port_state_t;

static freertos_port_state_t g_freertos_port = {0};

static bool freertos_scheduler_running(void) {
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

static void freertos_patch_task(void *argument) {
    (void)argument;

    bool previous_pressed = false;
    bool patch_applied = patch_is_active();

    if (patch_applied) {
        platform_console_write("[patch] ClearBitPatch already active.\r\n");
        print_patch_status();
    }

    for (;;) {
        bool pressed = bsp_board_button_state_get(BSP_BOARD_BUTTON_0);

        if (pressed && !previous_pressed && !patch_applied) {
            platform_console_write("[patch] S1 pressed. Applying ClearBitPatch...\r\n");

            if (patch_apply()) {
                patch_applied = true;
                platform_console_write(
                    "[patch] Patch applied online. D1 keeps blinking; D2 now marks the patched path.\r\n");
                print_patch_status();
            } else {
                platform_console_write("[patch] Patch apply failed.\r\n");
                print_patch_status();
            }
        }

        previous_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(FREERTOS_BUTTON_POLL_MS));
    }
}

static void freertos_victim_task(void *argument) {
    (void)argument;

    for (;;) {
        (void)patch_call();
        vTaskDelay(pdMS_TO_TICKS(FREERTOS_BLINK_INTERVAL_MS));
    }
}

bool freertos_port_init(void) {
    platform_console_init();
    led_demo_init();
    g_freertos_port.patch_mutex = xSemaphoreCreateMutex();

    if (g_freertos_port.patch_mutex == NULL) {
        return false;
    }

    platform_console_write(
        "\r\nClearBitPatch FreeRTOS LED demo ready.\r\n"
        "Board outputs without extra wiring: D1-D4 LEDs, RTT log over the existing debug USB link.\r\n"
        "Demo wiring-free indicators: D1 blinks in the original path, press S1 to hot-patch, then D2 stays on while D1 keeps the same blink cadence.\r\n");
    print_patch_status();
    return true;
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
        freertos_patch_task,
        "cb_patch",
        FREERTOS_PATCH_TASK_STACK_WORDS,
        NULL,
        FREERTOS_PATCH_TASK_PRIORITY,
        &g_freertos_port.patch_task);
    if (ok != pdPASS) {
        return false;
    }

    return true;
}

void platform_console_init(void) {
    SEGGER_RTT_Init();
    SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_NO_BLOCK_TRIM);
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
    (void)0;
}

bool platform_command_poll_line(char *buf, size_t buf_size, size_t *len) {
    (void)buf;
    (void)buf_size;
    (void)len;
    return false;
}

bool platform_command_prompt_u32(const char *prompt, uint32_t *out) {
    (void)prompt;
    (void)out;
    return false;
}

int platform_patch_call(platform_patch_entry_fn_t entry) {
    int result = 0;

    if (entry == NULL) {
        return 0;
    }

    if (!freertos_scheduler_running() || g_freertos_port.patch_mutex == NULL) {
        return entry();
    }

    (void)xSemaphoreTake(g_freertos_port.patch_mutex, portMAX_DELAY);
    result = entry();
    (void)xSemaphoreGive(g_freertos_port.patch_mutex);
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
    (void)0;
}

void platform_victim_resume(void) {
    (void)0;
}
