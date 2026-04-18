/*
 * board_port.c -- ESP32-C3 (RISC-V) flash operations for MorphPatch.
 *
 * Uses ESP-IDF partition API for flash read/write/erase. The ESP32-C3
 * maps code flash into the address space through an MMU/cache, so we
 * can read instructions via volatile pointers, but writes must go
 * through the SPI flash subsystem.
 */
#include "board_port.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_format.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include "hal/cache_hal.h"

#include "soc/soc_caps.h"

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "hal/usb_serial_jtag_ll.h"
#include "driver/usb_serial_jtag.h"
#endif

#include "driver/uart.h"
#include "esp_rom_sys.h"
#include "esp_rom_uart.h"

static const char *TAG = "board_port";
static uintptr_t s_last_patch_addr_early = 0u;

enum {
    BOARD_FLASH_SECTOR_BYTES = 4u * 1024u,
    BOARD_FLASH_SECTOR_WORDS = BOARD_FLASH_SECTOR_BYTES / sizeof(uint32_t),
    PATCH_SLOT_SIZE_BYTES = 4u,
};

/* ------------------------------------------------------------------ */
/*  Console backend                                                    */
/* ------------------------------------------------------------------ */

typedef enum {
    CONSOLE_BACKEND_NONE = 0,
    CONSOLE_BACKEND_USB_SERIAL_JTAG,
    CONSOLE_BACKEND_UART,
    CONSOLE_BACKEND_ROM,
} console_backend_t;

static console_backend_t s_console_backend = CONSOLE_BACKEND_NONE;

static bool try_init_usb_serial_jtag(void)
{
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 256,
        .rx_buffer_size = 256,
    };

    if (usb_serial_jtag_driver_install(&cfg) == ESP_OK) {
        s_console_backend = CONSOLE_BACKEND_USB_SERIAL_JTAG;
        return true;
    }
#endif
    return false;
}

static bool try_init_uart(void)
{
    const uart_port_t port = UART_NUM_0;
    const uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(port, 256, 256, 0, NULL, 0) != ESP_OK) {
        return false;
    }

    if (uart_param_config(port, &uart_cfg) != ESP_OK) {
        uart_driver_delete(port);
        return false;
    }

    s_console_backend = CONSOLE_BACKEND_UART;
    return true;
}

static void init_rom_fallback(void)
{
    s_console_backend = CONSOLE_BACKEND_ROM;
}

bool board_console_init(void)
{
    if (s_console_backend != CONSOLE_BACKEND_NONE) {
        return true;
    }

    if (try_init_usb_serial_jtag()) {
        return true;
    }

    if (try_init_uart()) {
        return true;
    }

    init_rom_fallback();
    return true;
}

const char *board_console_backend_name(void)
{
    switch (s_console_backend) {
        case CONSOLE_BACKEND_USB_SERIAL_JTAG:
            return "USB-Serial-JTAG";
        case CONSOLE_BACKEND_UART:
            return "UART0";
        case CONSOLE_BACKEND_ROM:
            return "ROM";
        default:
            return "none";
    }
}

static void board_console_write_char(char ch)
{
    switch (s_console_backend) {
#if SOC_USB_SERIAL_JTAG_SUPPORTED
        case CONSOLE_BACKEND_USB_SERIAL_JTAG:
            usb_serial_jtag_write_bytes((const uint8_t *)&ch, 1, portMAX_DELAY);
            break;
#endif
        case CONSOLE_BACKEND_UART:
            uart_write_bytes(UART_NUM_0, &ch, 1);
            break;
        case CONSOLE_BACKEND_ROM:
        default:
            esp_rom_output_putc(ch);
            break;
    }
}

void board_console_write(const char *s)
{
    if (s == NULL) {
        return;
    }

    (void)board_console_init();

    while (*s != '\0') {
        board_console_write_char(*s);
        ++s;
    }
}

void board_console_printf(const char *fmt, ...)
{
    char buffer[256];
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (written <= 0) {
        return;
    }

    buffer[sizeof(buffer) - 1u] = '\0';
    board_console_write(buffer);
}

int board_console_getchar_nonblock(void)
{
    uint8_t ch = 0u;

    (void)board_console_init();

    switch (s_console_backend) {
#if SOC_USB_SERIAL_JTAG_SUPPORTED
        case CONSOLE_BACKEND_USB_SERIAL_JTAG:
            if (usb_serial_jtag_read_bytes(&ch, 1, 0) == 1) {
                return (int)ch;
            }
            return -1;
#endif
        case CONSOLE_BACKEND_UART:
            if (uart_read_bytes(UART_NUM_0, &ch, 1, 0) == 1) {
                return (int)ch;
            }
            return -1;

        case CONSOLE_BACKEND_ROM:
        default: {
            int c = esp_rom_output_rx_one_char(&ch);
            if (c == 0) {
                return (int)ch;
            }
            return -1;
        }
    }
}

void board_console_putchar(char c)
{
    (void)board_console_init();
    board_console_write_char(c);
}

/* ------------------------------------------------------------------ */
/*  Flash helpers                                                      */
/* ------------------------------------------------------------------ */

static const esp_partition_t *board_get_running_partition(void)
{
    return esp_ota_get_running_partition();
}

/*
 * Translate a memory-mapped code address into a physical flash offset
 * relative to the running partition.
 */
static bool board_patch_locate_word(uintptr_t addr, const esp_partition_t **part, size_t *offset)
{
    const esp_partition_t *running = NULL;
    size_t phys_addr = 0u;

    if ((part == NULL) || (offset == NULL)) {
        return false;
    }

    phys_addr = (size_t)spi_flash_cache2phys((const void *)addr);
    if (phys_addr == SPI_FLASH_CACHE2PHYS_FAIL) {
        ESP_LOGE(TAG, "cache2phys failed for 0x%08lX", (unsigned long)addr);
        return false;
    }

    running = board_get_running_partition();
    if (running == NULL) {
        ESP_LOGE(TAG, "cannot determine running partition");
        return false;
    }

    if ((phys_addr < running->address) ||
        ((phys_addr + PATCH_SLOT_SIZE_BYTES) > (running->address + running->size))) {
        ESP_LOGE(TAG, "phys 0x%08X outside partition [0x%08lX..+0x%08lX)",
                 (unsigned)phys_addr,
                 (unsigned long)running->address,
                 (unsigned long)running->size);
        return false;
    }

    *part = running;
    *offset = phys_addr - running->address;
    return true;
}

static uint32_t board_pack_slot_word(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0])
         | ((uint32_t)bytes[1] << 8)
         | ((uint32_t)bytes[2] << 16)
         | ((uint32_t)bytes[3] << 24);
}

bool board_patch_read_word(uintptr_t addr, uint32_t *out_word)
{
    size_t phys_addr = (size_t)spi_flash_cache2phys((const void *)addr);
    uint8_t raw[4];

    if ((out_word == NULL) || (phys_addr == SPI_FLASH_CACHE2PHYS_FAIL)) {
        return false;
    }

    if (esp_flash_read(esp_flash_default_chip, raw, phys_addr, sizeof(raw)) != ESP_OK) {
        return false;
    }

    *out_word = board_pack_slot_word(raw);
    return true;
}

bool board_patch_write_word_monotonic(uintptr_t addr, uint32_t oldv, uint32_t newv)
{
    size_t phys_addr = (size_t)spi_flash_cache2phys((const void *)addr);
    uint32_t verify = 0u;
    uint8_t write_buf[4];
    uint8_t cur[4];

    if ((newv & ~oldv) != 0u) {
        board_console_printf("[patch-debug] monotonic violation: newv=0x%08lX oldv=0x%08lX\r\n",
                             (unsigned long)newv, (unsigned long)oldv);
        return false;
    }

    if (phys_addr == SPI_FLASH_CACHE2PHYS_FAIL) {
        return false;
    }

    /* Verify current content matches expected old value. */
    if (esp_flash_read(esp_flash_default_chip, cur, phys_addr, sizeof(cur)) != ESP_OK) {
        return false;
    }
    if (board_pack_slot_word(cur) != oldv) {
        board_console_printf("[patch-debug] current 0x%08lX != expected 0x%08lX\r\n",
                             (unsigned long)board_pack_slot_word(cur),
                             (unsigned long)oldv);
        return false;
    }

    /* Pack new value little-endian. */
    write_buf[0] = (uint8_t)(newv & 0xFFu);
    write_buf[1] = (uint8_t)((newv >> 8) & 0xFFu);
    write_buf[2] = (uint8_t)((newv >> 16) & 0xFFu);
    write_buf[3] = (uint8_t)((newv >> 24) & 0xFFu);

    if (esp_flash_write(esp_flash_default_chip, write_buf, phys_addr, sizeof(write_buf)) != ESP_OK) {
        board_console_printf("[patch-debug] esp_flash_write failed\r\n");
        return false;
    }

    s_last_patch_addr_early = addr;
    board_code_sync();

    /* Verify. */
    {
        uint8_t readback[4];
        if (esp_flash_read(esp_flash_default_chip, readback, phys_addr, sizeof(readback)) != ESP_OK) {
            return false;
        }
        verify = board_pack_slot_word(readback);
    }

    if (verify != newv) {
        board_console_printf("[patch-debug] verify failed: got 0x%08lX expected 0x%08lX\r\n",
                             (unsigned long)verify, (unsigned long)newv);
        return false;
    }

    return true;
}

bool board_patch_write_word_erase_rewrite(uintptr_t addr, uint32_t oldv, uint32_t newv)
{
    const esp_partition_t *part = NULL;
    size_t offset = 0u;
    size_t phys_sector = 0u;
    size_t sector_offset = 0u;
    size_t word_offset_in_sector = 0u;
    uint8_t *sector_buf = NULL;
    bool ok = false;

    if (!board_patch_locate_word(addr, &part, &offset)) {
        return false;
    }

    /* Verify current value. */
    {
        uint8_t cur[4];
        if (esp_partition_read_raw(part, offset, cur, sizeof(cur)) != ESP_OK) {
            return false;
        }
        if (board_pack_slot_word(cur) != oldv) {
            return false;
        }
    }

    sector_offset = offset & ~((size_t)BOARD_FLASH_SECTOR_BYTES - 1u);
    word_offset_in_sector = offset - sector_offset;
    phys_sector = part->address + sector_offset;

    sector_buf = (uint8_t *)malloc(BOARD_FLASH_SECTOR_BYTES);
    if (sector_buf == NULL) {
        board_console_printf("[patch-debug] erase-rewrite malloc failed\r\n");
        return false;
    }

    /* Read full sector via low-level API. */
    if (esp_flash_read(esp_flash_default_chip, sector_buf, phys_sector, BOARD_FLASH_SECTOR_BYTES) != ESP_OK) {
        board_console_printf("[patch-debug] sector read failed\r\n");
        goto cleanup;
    }

    /* Modify the target word in the buffer. */
    sector_buf[word_offset_in_sector + 0u] = (uint8_t)(newv & 0xFFu);
    sector_buf[word_offset_in_sector + 1u] = (uint8_t)((newv >> 8) & 0xFFu);
    sector_buf[word_offset_in_sector + 2u] = (uint8_t)((newv >> 16) & 0xFFu);
    sector_buf[word_offset_in_sector + 3u] = (uint8_t)((newv >> 24) & 0xFFu);

    /* Erase the sector using low-level API (bypasses partition protection). */
    if (esp_flash_erase_region(esp_flash_default_chip, phys_sector, BOARD_FLASH_SECTOR_BYTES) != ESP_OK) {
        board_console_printf("[patch-debug] sector erase failed\r\n");
        goto cleanup;
    }

    /* Write back the full sector. */
    if (esp_flash_write(esp_flash_default_chip, sector_buf, phys_sector, BOARD_FLASH_SECTOR_BYTES) != ESP_OK) {
        board_console_printf("[patch-debug] sector write-back failed\r\n");
        goto cleanup;
    }

    s_last_patch_addr_early = addr;
    board_code_sync();

    /* Verify. */
    {
        uint8_t readback[4];
        uint32_t verify = 0u;

        if (esp_flash_read(esp_flash_default_chip, readback, (size_t)spi_flash_cache2phys((const void *)addr), sizeof(readback)) != ESP_OK) {
            goto cleanup;
        }
        verify = board_pack_slot_word(readback);
        if (verify != newv) {
            board_console_printf("[patch-debug] erase-rewrite verify failed: got 0x%08lX expected 0x%08lX\r\n",
                                 (unsigned long)verify, (unsigned long)newv);
            goto cleanup;
        }
    }

    ok = true;

cleanup:
    free(sector_buf);
    return ok;
}

bool board_patch_write_word_with_erase(uintptr_t addr, uint32_t oldv, uint32_t newv)
{
    return board_patch_write_word_erase_rewrite(addr, oldv, newv);
}

void board_code_sync(void)
{
    /* Invalidate the entire ICache to pick up flash changes. */
    cache_hal_invalidate_addr(0x42000000u, 0x00800000u);
    __asm__ volatile("fence rw, rw" ::: "memory");
    __asm__ volatile("fence.i" ::: "memory");
}
