#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "SEGGER_RTT.h"

typedef uint32_t UBaseType_t;

typedef enum {
    APP_EXEC_MODE_INTERACTIVE = 0,
    APP_EXEC_MODE_DEMO = 1,
    APP_EXEC_MODE_BENCHMARK = 2,
} app_exec_mode_t;

typedef struct {
    UBaseType_t queue_length;
    UBaseType_t item_size;
    uint8_t    *p_mem;
    uint32_t    alloc_size;
    uint8_t    *p_guard;
} txfr_queue_t;

#define SIM_HEAP_SIZE  2048u
#define GUARD_SIZE     16u
#define FILL_BYTE      0x5Au
#define GUARD_BYTE     0xCCu
#define WRITE_BYTE     0xA5u

void console_init(void);
void console_puts(const char *s);
void console_prompt(void);

void app_set_exec_mode(app_exec_mode_t mode);
app_exec_mode_t app_get_exec_mode(void);
bool app_exec_mode_is_verbose(void);
bool app_fetch_auto_inputs(UBaseType_t *queue_length, UBaseType_t *item_size);
void app_get_attack_inputs(UBaseType_t *queue_length, UBaseType_t *item_size);

int fun1(void);
int fun2(void);
int rapid_vuln_target(UBaseType_t queue_length, UBaseType_t item_size);

#define PROMPT_RTT_U32(prompt_msg, out_var) do {                        \
    char buf[24];                                                       \
    int idx = 0;                                                        \
    int c;                                                              \
    console_puts(prompt_msg);                                           \
    while (idx < 23) {                                                  \
        do { c = SEGGER_RTT_GetKey(); } while (c < 0);                  \
        if (c == '\r' || c == '\n') {                                   \
            if (idx == 0) continue;                                     \
            break;                                                      \
        }                                                               \
        if (c == '\b' || c == 0x7F) {                                   \
            if (idx > 0) {                                              \
                idx--;                                                  \
                console_puts("\b \b");                                  \
            }                                                           \
        } else {                                                        \
            buf[idx++] = (char)c;                                       \
            SEGGER_RTT_PutChar(0, (char)c);                             \
        }                                                               \
    }                                                                   \
    buf[idx] = '\0';                                                    \
    console_puts("\r\n");                                               \
    uint32_t val = 0;                                                   \
    if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {            \
        for (int i = 2; buf[i] != '\0'; i++) {                          \
            uint8_t d = 0;                                              \
            if (buf[i] >= '0' && buf[i] <= '9') d = (uint8_t)(buf[i] - '0'); \
            else if (buf[i] >= 'a' && buf[i] <= 'f') d = (uint8_t)(buf[i] - 'a' + 10); \
            else if (buf[i] >= 'A' && buf[i] <= 'F') d = (uint8_t)(buf[i] - 'A' + 10); \
            val = (val << 4) | d;                                       \
        }                                                               \
    } else {                                                            \
        for (int i = 0; buf[i] != '\0'; i++) {                          \
            val = val * 10u + (uint32_t)(buf[i] - '0');                 \
        }                                                               \
    }                                                                   \
    out_var = val;                                                      \
} while (0)

#endif
