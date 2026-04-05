#include "ports/common/platform_port.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "core/app/clearbit_app.h"
#include "nuttx_port.h"

#ifdef __NuttX__

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    NUTTX_CONTROL_TASK_STACK_BYTES = 4096u,
    NUTTX_VICTIM_TASK_STACK_BYTES = 4096u,
};

typedef struct {
    pthread_mutex_t patch_mutex;
    pthread_mutex_t call_mutex;
    sem_t victim_kick_sem;
    sem_t request_done_sem;
    sem_t suspend_ack_sem;
    sem_t resume_sem;
    pthread_t control_task;
    pthread_t victim_task;
    platform_patch_entry_fn_t pending_entry;
    int pending_result;
    volatile bool suspend_requested;
    bool initialized;
    bool started;
} nuttx_port_state_t;

static nuttx_port_state_t g_nuttx_port = {0};

static void sleep_1ms(void) {
    (void)usleep(1000);
}

static bool wait_sem_uninterruptible(sem_t *sem) {
    int rc = 0;

    if (sem == NULL) {
        return false;
    }

    do {
        rc = sem_wait(sem);
    } while (rc < 0 && errno == EINTR);

    return rc == 0;
}

static void drain_sem(sem_t *sem) {
    if (sem == NULL) {
        return;
    }

    while (sem_trywait(sem) == 0) {
    }
}

static int read_key_nonblocking(void) {
    unsigned char key = 0u;
    ssize_t read_len = 0;

    read_len = read(STDIN_FILENO, &key, 1);
    if (read_len == 1) {
        return (int)key;
    }

    if (read_len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return -1;
    }

    return -1;
}

static int read_key_blocking(void) {
    int key = -1;

    do {
        key = read_key_nonblocking();
        if (key < 0) {
            sleep_1ms();
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

static void *nuttx_control_task(void *argument) {
    (void)argument;

    clearbit_app_boot();

    for (;;) {
        clearbit_app_process_once();
        sleep_1ms();
    }
}

static void *nuttx_victim_task(void *argument) {
    (void)argument;

    for (;;) {
        platform_patch_entry_fn_t entry = NULL;
        int result = 0;

        if (g_nuttx_port.suspend_requested) {
            (void)sem_post(&g_nuttx_port.suspend_ack_sem);
            (void)wait_sem_uninterruptible(&g_nuttx_port.resume_sem);
            drain_sem(&g_nuttx_port.victim_kick_sem);
        }

        (void)wait_sem_uninterruptible(&g_nuttx_port.victim_kick_sem);
        entry = g_nuttx_port.pending_entry;
        if (entry == NULL) {
            (void)sem_post(&g_nuttx_port.request_done_sem);
            continue;
        }

        (void)pthread_mutex_lock(&g_nuttx_port.patch_mutex);
        result = entry();
        g_nuttx_port.pending_result = result;
        (void)pthread_mutex_unlock(&g_nuttx_port.patch_mutex);

        (void)sem_post(&g_nuttx_port.request_done_sem);
    }
}

static bool create_task(pthread_t *task,
                        void *(*entry)(void *),
                        size_t stack_size) {
    pthread_attr_t attr;
    int rc = 0;

    if (task == NULL || entry == NULL) {
        return false;
    }

    rc = pthread_attr_init(&attr);
    if (rc != 0) {
        return false;
    }

#ifdef PTHREAD_STACK_MIN
    if (stack_size < (size_t)PTHREAD_STACK_MIN) {
        stack_size = (size_t)PTHREAD_STACK_MIN;
    }
#endif

    (void)pthread_attr_setstacksize(&attr, stack_size);
    rc = pthread_create(task, &attr, entry, NULL);
    (void)pthread_attr_destroy(&attr);

    return rc == 0;
}

bool nuttx_port_init(void) {
    if (g_nuttx_port.initialized) {
        return true;
    }

    if (pthread_mutex_init(&g_nuttx_port.patch_mutex, NULL) != 0) {
        return false;
    }

    if (pthread_mutex_init(&g_nuttx_port.call_mutex, NULL) != 0) {
        (void)pthread_mutex_destroy(&g_nuttx_port.patch_mutex);
        return false;
    }

    if (sem_init(&g_nuttx_port.victim_kick_sem, 0, 0) != 0) {
        (void)pthread_mutex_destroy(&g_nuttx_port.call_mutex);
        (void)pthread_mutex_destroy(&g_nuttx_port.patch_mutex);
        return false;
    }

    if (sem_init(&g_nuttx_port.request_done_sem, 0, 0) != 0) {
        (void)sem_destroy(&g_nuttx_port.victim_kick_sem);
        (void)pthread_mutex_destroy(&g_nuttx_port.call_mutex);
        (void)pthread_mutex_destroy(&g_nuttx_port.patch_mutex);
        return false;
    }

    if (sem_init(&g_nuttx_port.suspend_ack_sem, 0, 0) != 0) {
        (void)sem_destroy(&g_nuttx_port.request_done_sem);
        (void)sem_destroy(&g_nuttx_port.victim_kick_sem);
        (void)pthread_mutex_destroy(&g_nuttx_port.call_mutex);
        (void)pthread_mutex_destroy(&g_nuttx_port.patch_mutex);
        return false;
    }

    if (sem_init(&g_nuttx_port.resume_sem, 0, 0) != 0) {
        (void)sem_destroy(&g_nuttx_port.suspend_ack_sem);
        (void)sem_destroy(&g_nuttx_port.request_done_sem);
        (void)sem_destroy(&g_nuttx_port.victim_kick_sem);
        (void)pthread_mutex_destroy(&g_nuttx_port.call_mutex);
        (void)pthread_mutex_destroy(&g_nuttx_port.patch_mutex);
        return false;
    }

    g_nuttx_port.pending_entry = NULL;
    g_nuttx_port.pending_result = 0;
    g_nuttx_port.suspend_requested = false;
    g_nuttx_port.initialized = true;
    g_nuttx_port.started = false;
    return true;
}

bool nuttx_port_start(void) {
    if (!g_nuttx_port.initialized) {
        return false;
    }

    if (g_nuttx_port.started) {
        return true;
    }

    if (!create_task(
            &g_nuttx_port.victim_task,
            nuttx_victim_task,
            (size_t)NUTTX_VICTIM_TASK_STACK_BYTES)) {
        return false;
    }

    if (!create_task(
            &g_nuttx_port.control_task,
            nuttx_control_task,
            (size_t)NUTTX_CONTROL_TASK_STACK_BYTES)) {
        return false;
    }

    g_nuttx_port.started = true;
    return true;
}

void platform_console_init(void) {
    int flags = 0;

    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0 && (flags & O_NONBLOCK) == 0) {
        (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    platform_console_write("\r\nNuttX console ready. (ClearBitPatch Demo)\r\n");
}

void platform_console_write(const char *text) {
    if (text == NULL) {
        return;
    }

    (void)fputs(text, stdout);
    (void)fflush(stdout);
}

int platform_console_vprintf(const char *format, va_list *args) {
    if (format == NULL || args == NULL) {
        return 0;
    }

    return vprintf(format, *args);
}

void platform_command_prompt(void) {
    platform_console_write("nuttx> ");
}

bool platform_command_poll_line(char *buf, size_t buf_size, size_t *len) {
    int key = 0;

    if (buf == NULL || len == NULL || buf_size == 0u) {
        return false;
    }

    key = read_key_nonblocking();
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
        (void)fputc((char)key, stdout);
        (void)fflush(stdout);
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
        (void)fputc((char)key, stdout);
        (void)fflush(stdout);
    }

    buf[len] = '\0';
    platform_console_write("\r\n");
    return parse_u32_string(buf, out);
}

int platform_patch_call(platform_patch_entry_fn_t entry) {
    int result = 0;

    if (entry == NULL) {
        return 0;
    }

    if (!g_nuttx_port.started) {
        return entry();
    }

    if (pthread_equal(pthread_self(), g_nuttx_port.victim_task)) {
        (void)pthread_mutex_lock(&g_nuttx_port.patch_mutex);
        result = entry();
        (void)pthread_mutex_unlock(&g_nuttx_port.patch_mutex);
        return result;
    }

    if (pthread_mutex_lock(&g_nuttx_port.call_mutex) != 0) {
        return entry();
    }

    drain_sem(&g_nuttx_port.request_done_sem);
    g_nuttx_port.pending_entry = entry;
    (void)sem_post(&g_nuttx_port.victim_kick_sem);

    if (!wait_sem_uninterruptible(&g_nuttx_port.request_done_sem)) {
        g_nuttx_port.pending_entry = NULL;
        (void)pthread_mutex_unlock(&g_nuttx_port.call_mutex);
        return entry();
    }

    result = g_nuttx_port.pending_result;
    g_nuttx_port.pending_entry = NULL;
    (void)pthread_mutex_unlock(&g_nuttx_port.call_mutex);

    return result;
}

void platform_lock_patch_region(void) {
    if (!g_nuttx_port.started) {
        return;
    }

    (void)pthread_mutex_lock(&g_nuttx_port.patch_mutex);
}

void platform_unlock_patch_region(void) {
    if (!g_nuttx_port.started) {
        return;
    }

    (void)pthread_mutex_unlock(&g_nuttx_port.patch_mutex);
}

void platform_victim_suspend(void) {
    if (!g_nuttx_port.started) {
        return;
    }

    if (pthread_equal(pthread_self(), g_nuttx_port.victim_task)) {
        return;
    }

    drain_sem(&g_nuttx_port.suspend_ack_sem);
    g_nuttx_port.suspend_requested = true;

    /*
     * The victim checks suspend_requested at the top of its loop (a safe
     * point where it holds no mutex).  If it is currently blocked on
     * victim_kick_sem, wake it so it can observe the flag.
     */
    (void)sem_post(&g_nuttx_port.victim_kick_sem);
    (void)wait_sem_uninterruptible(&g_nuttx_port.suspend_ack_sem);
}

void platform_victim_resume(void) {
    if (!g_nuttx_port.started) {
        return;
    }

    if (pthread_equal(pthread_self(), g_nuttx_port.victim_task)) {
        return;
    }

    g_nuttx_port.suspend_requested = false;
    (void)sem_post(&g_nuttx_port.resume_sem);
}

#else

#include "SEGGER_RTT.h"

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

bool nuttx_port_init(void) {
    return true;
}

bool nuttx_port_start(void) {
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
    platform_console_write("nuttx> ");
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
