#include "queue_demo.h"

#include <string.h>

#include "console.h"
#include "patch_result.h"

enum {
    SIM_HEAP_SIZE = 2048u,
    GUARD_SIZE = 16u,
    FILL_BYTE = 0x5Au,
    GUARD_BYTE = 0xCCu,
    WRITE_BYTE = 0xA5u,
};

int queue_demo_run(UBaseType_t uxQueueLength,
                   UBaseType_t uxItemSize,
                   bool verbose,
                   const queue_demo_profile_t *profile)
{
    static uint8_t sim_heap[SIM_HEAP_SIZE];
    size_t sim_heap_off = 0u;

    if (profile == NULL) {
        return -99;
    }

    if (verbose) {
        console_puts(profile->banner);
        if ((profile->target_name != NULL) && (profile->target_name[0] != '\0')) {
            console_printf("Target: %s\r\n", profile->target_name);
        }
        console_puts(profile->status_line);
        console_printf(
            "\r\n[Input]\r\n"
            "  uxQueueLength = 0x%08lX\r\n"
            "  uxItemSize    = 0x%08lX\r\n",
            (unsigned long)uxQueueLength,
            (unsigned long)uxItemSize);
    }

    if (profile->validate_before_alloc) {
        int check_ret = 0;

        if (uxQueueLength == 0u) {
            check_ret = -1;
        } else if (uxItemSize == 0u) {
            check_ret = -2;
        } else if (uxQueueLength > (0xFFFFFFFFu / uxItemSize)) {
            check_ret = -3;
        }

        if (check_ret != 0) {
            if (verbose) {
                console_printf(
                    "\r\n[%s] Parameter check rejected request, ret=%d\r\n",
                    profile->reject_prefix,
                    check_ret);
                if (check_ret == -3) {
                    console_puts(profile->reject_wrap_line);
                }
                console_puts(profile->reject_abort_line);
            }
            return check_ret;
        }
    }

    {
        uint32_t mem_size = uxQueueLength * uxItemSize;
        txfr_queue_t *q = NULL;
        uint8_t *buf_ptr = NULL;
        uint8_t *guard_ptr = NULL;
        uint64_t exp_64;
        uint32_t target_index = 1u;
        uint64_t write_off64;
        uint64_t write_end64;
        uint8_t *write_ptr;

        if ((sim_heap_off + sizeof(txfr_queue_t)) <= SIM_HEAP_SIZE) {
            q = (txfr_queue_t *)&sim_heap[sim_heap_off];
            sim_heap_off += sizeof(txfr_queue_t);
        } else {
            if (verbose) {
                console_puts("[-] queue object alloc failed\r\n");
            }
            return -10;
        }

        if ((sim_heap_off + mem_size) <= SIM_HEAP_SIZE) {
            buf_ptr = &sim_heap[sim_heap_off];
            sim_heap_off += mem_size;
        } else if (mem_size != 0u) {
            if (verbose) {
                console_puts("[-] queue buffer alloc failed\r\n");
            }
            return -11;
        }

        if ((sim_heap_off + GUARD_SIZE) <= SIM_HEAP_SIZE) {
            guard_ptr = &sim_heap[sim_heap_off];
            sim_heap_off += GUARD_SIZE;
        } else {
            if (verbose) {
                console_puts("[-] guard alloc failed\r\n");
            }
            return -12;
        }

        if (mem_size != 0u) {
            memset(buf_ptr, FILL_BYTE, mem_size);
        }
        memset(guard_ptr, GUARD_BYTE, GUARD_SIZE);

        q->queue_length = uxQueueLength;
        q->item_size = uxItemSize;
        q->p_mem = buf_ptr;
        q->alloc_size = mem_size;
        q->p_guard = guard_ptr;

        exp_64 = (uint64_t)uxQueueLength * (uint64_t)uxItemSize;
        if (verbose) {
            console_printf(
                "\r\n[Create result]\r\n"
                "  expected bytes (64-bit) = 0x%08lX%08lX\r\n"
                "  alloc_size   (32-bit)   = 0x%08lX\r\n",
                (unsigned long)(exp_64 >> 32),
                (unsigned long)exp_64,
                (unsigned long)q->alloc_size);
        }

        if ((uxItemSize == 0u) || (uxQueueLength < 2u)) {
            if (verbose) {
                console_puts("\r\n[-] Need uxQueueLength >= 2 and uxItemSize > 0 to simulate write.\r\n");
            }
            return PATCH_RESULT_SAFE_NOOP;
        }

        write_off64 = (uint64_t)target_index * (uint64_t)q->item_size;
        write_end64 = write_off64 + (uint64_t)q->item_size;
        write_ptr = q->p_mem + (size_t)write_off64;

        if (verbose) {
            int i;
            console_printf("\r\n[Send simulation]\r\n  writing to logical index = %lu\r\n",
                           (unsigned long)target_index);
            console_puts("  guard before           = ");
            for (i = 0; i < (int)GUARD_SIZE; ++i) {
                console_printf("%02X ", q->p_guard[i]);
            }
            console_puts("\r\n");
        }

        if (write_end64 > q->alloc_size) {
            size_t safe_span = (size_t)q->alloc_size + GUARD_SIZE;

            if (verbose) {
                console_puts("  [!] Logical write exceeds actual allocated buffer.\r\n");
            }

            if (write_off64 < safe_span) {
                size_t can_write = (write_end64 > safe_span)
                    ? (size_t)(safe_span - write_off64)
                    : (size_t)q->item_size;
                memset(write_ptr, WRITE_BYTE, can_write);
            }

            if (verbose) {
                int i;
                console_puts("  guard after            = ");
                for (i = 0; i < (int)GUARD_SIZE; ++i) {
                    console_printf("%02X ", q->p_guard[i]);
                }
                console_puts("\r\n  [-] Heap-buffer-overflow effect observed. Guard overwritten!\r\n");
            }
            return PATCH_RESULT_ATTACK_OVERFLOW;
        }

        memset(write_ptr, WRITE_BYTE, q->item_size);

        if (verbose) {
            int i;
            console_puts("  guard after            = ");
            for (i = 0; i < (int)GUARD_SIZE; ++i) {
                console_printf("%02X ", q->p_guard[i]);
            }
            console_puts("\r\n  [+] Safe write confirmed. No overflow.\r\n");
        }
    }

    if (verbose) {
        console_puts(profile->done_line);
    }

    return PATCH_RESULT_SAFE_EXECUTED;
}
