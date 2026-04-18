/*
 * board_port.c -- ESP32-S3 board abstraction for MorphPatch.
 *
 * Console: USB Serial/JTAG (default on ESP32-S3 DevKitC-1) or UART.
 * Flash:   3-byte Xtensa patch words, read/write via esp_partition raw APIs.
 * Cache:   cache_hal_invalidate_addr after flash writes + memw/isync barrier.
 */
#include "board_port.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_flash.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include "hal/cache_hal.h"

#include "soc/soc_caps.h"

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"
#endif

#include "driver/uart.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PATCH_SLOT_SIZE_BYTES 3u

typedef enum {
    CONSOLE_BACKEND_ROM = 0,
    CONSOLE_BACKEND_USB_SERIAL_JTAG = 1,
    CONSOLE_BACKEND_UART = 2,
} console_backend_t;

typedef struct {
    const esp_partition_t *running;
    size_t part_offs;
} board_patch_location_t;

static console_backend_t s_console_backend = CONSOLE_BACKEND_ROM;
static uintptr_t s_last_patch_addr = 0u;
static size_t s_last_patch_size = PATCH_SLOT_SIZE_BYTES;

/* ------------------------------------------------------------------ */
/*  Flash address translation                                          */
/* ------------------------------------------------------------------ */

static bool board_patch_locate_word(uintptr_t addr, board_patch_location_t *location)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    size_t phys_offs;

    if ((location == NULL) || (running == NULL)) {
        return false;
    }

    phys_offs = spi_flash_cache2phys((const void *)addr);
    if ((phys_offs == SPI_FLASH_CACHE2PHYS_FAIL) ||
        (phys_offs < running->address) ||
        ((phys_offs + PATCH_SLOT_SIZE_BYTES) > (running->address + running->size))) {
        return false;
    }

    location->running = running;
    location->part_offs = phys_offs - running->address;
    return true;
}

static void board_pack_slot_word(uint32_t word, uint8_t out_bytes[PATCH_SLOT_SIZE_BYTES])
{
    out_bytes[0] = (uint8_t)(word & 0xFFu);
    out_bytes[1] = (uint8_t)((word >> 8u) & 0xFFu);
    out_bytes[2] = (uint8_t)((word >> 16u) & 0xFFu);
}

/* ------------------------------------------------------------------ */
/*  Console                                                            */
/* ------------------------------------------------------------------ */

static void board_rom_write_fallback(const char *s)
{
    if (s == NULL) {
        return;
    }
    while (*s != '\0') {
        esp_rom_printf("%c", *s);
        ++s;
    }
}

static bool board_console_init_uart_backend(void)
{
    if (!uart_is_driver_installed((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM)) {
        const uart_config_t uart_cfg = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_DEFAULT,
        };

        if (uart_driver_install((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0) != ESP_OK) {
            s_console_backend = CONSOLE_BACKEND_ROM;
            return false;
        }

        if (uart_param_config((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, &uart_cfg) != ESP_OK) {
            s_console_backend = CONSOLE_BACKEND_ROM;
            return false;
        }

        (void)uart_set_pin((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }

    s_console_backend = CONSOLE_BACKEND_UART;
    return true;
}

bool board_console_init(void)
{
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    if (usb_serial_jtag_is_driver_installed()) {
        s_console_backend = CONSOLE_BACKEND_USB_SERIAL_JTAG;
        return true;
    }

    {
        usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        if (usb_serial_jtag_driver_install(&usb_cfg) == ESP_OK) {
            s_console_backend = CONSOLE_BACKEND_USB_SERIAL_JTAG;
            vTaskDelay(pdMS_TO_TICKS(50));
            return true;
        }
    }
#endif

    return board_console_init_uart_backend();
}

void board_console_write(const char *s)
{
    const size_t len = (s == NULL) ? 0u : strlen(s);

    if (len == 0u) {
        return;
    }

    switch (s_console_backend) {
        case CONSOLE_BACKEND_USB_SERIAL_JTAG:
#if SOC_USB_SERIAL_JTAG_SUPPORTED
            (void)usb_serial_jtag_write_bytes(s, len, pdMS_TO_TICKS(20));
            (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(20));
#endif
            break;
        case CONSOLE_BACKEND_UART:
            (void)uart_write_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, s, len);
            break;
        case CONSOLE_BACKEND_ROM:
        default:
            board_rom_write_fallback(s);
            break;
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
    int read_len = 0;

    switch (s_console_backend) {
        case CONSOLE_BACKEND_USB_SERIAL_JTAG:
#if SOC_USB_SERIAL_JTAG_SUPPORTED
            read_len = usb_serial_jtag_read_bytes(&ch, 1u, 0);
#endif
            break;
        case CONSOLE_BACKEND_UART:
            read_len = uart_read_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, &ch, 1u, 0);
            break;
        case CONSOLE_BACKEND_ROM:
        default:
            read_len = 0;
            break;
    }

    return (read_len == 1) ? (int)ch : -1;
}

void board_console_putchar(char c)
{
    const char out[2] = { c, '\0' };
    board_console_write(out);
}

const char *board_console_backend_name(void)
{
    switch (s_console_backend) {
        case CONSOLE_BACKEND_USB_SERIAL_JTAG:
            return "usb-serial-jtag";
        case CONSOLE_BACKEND_UART:
            return "uart";
        case CONSOLE_BACKEND_ROM:
        default:
            return "rom-fallback";
    }
}

/* ------------------------------------------------------------------ */
/*  Flash operations                                                   */
/* ------------------------------------------------------------------ */

uint32_t board_patch_read_word(uintptr_t addr)
{
    size_t phys = spi_flash_cache2phys((const void *)addr);
    uint8_t raw_bytes[PATCH_SLOT_SIZE_BYTES];

    if (phys == SPI_FLASH_CACHE2PHYS_FAIL) {
        return 0xFFFFFFFFu;
    }

    if (esp_flash_read(esp_flash_default_chip, raw_bytes, phys, sizeof(raw_bytes)) != ESP_OK) {
        return 0xFFFFFFFFu;
    }

    return ((uint32_t)raw_bytes[0]) |
           ((uint32_t)raw_bytes[1] << 8u) |
           ((uint32_t)raw_bytes[2] << 16u);
}

bool board_patch_write_word_monotonic(uintptr_t addr, uint32_t oldv, uint32_t newv)
{
    size_t phys = spi_flash_cache2phys((const void *)addr);
    uint8_t expected_bytes[PATCH_SLOT_SIZE_BYTES];
    uint8_t new_bytes[PATCH_SLOT_SIZE_BYTES];
    uint8_t raw_bytes[PATCH_SLOT_SIZE_BYTES];
    const uint32_t mask = 0x00FFFFFFu;

    if ((((newv & ~oldv) & mask) != 0u) || (phys == SPI_FLASH_CACHE2PHYS_FAIL)) {
        return false;
    }

    board_pack_slot_word(oldv, expected_bytes);
    board_pack_slot_word(newv, new_bytes);

    if (esp_flash_read(esp_flash_default_chip, raw_bytes, phys, sizeof(raw_bytes)) != ESP_OK) {
        return false;
    }

    if (memcmp(raw_bytes, expected_bytes, sizeof(raw_bytes)) != 0) {
        return false;
    }

    if (esp_flash_write(esp_flash_default_chip, new_bytes, phys, sizeof(new_bytes)) != ESP_OK) {
        return false;
    }

    s_last_patch_addr = addr;
    s_last_patch_size = PATCH_SLOT_SIZE_BYTES;
    board_code_sync();

    if (esp_flash_read(esp_flash_default_chip, raw_bytes, phys, sizeof(raw_bytes)) != ESP_OK) {
        return false;
    }

    if (memcmp(raw_bytes, new_bytes, sizeof(raw_bytes)) != 0) {
        return false;
    }

    return true;
}

bool board_patch_write_word_erase_rewrite(uintptr_t addr, uint32_t oldv, uint32_t newv)
{
    size_t phys = spi_flash_cache2phys((const void *)addr);
    uint8_t expected_bytes[PATCH_SLOT_SIZE_BYTES];
    uint8_t new_bytes[PATCH_SLOT_SIZE_BYTES];
    uint8_t raw_bytes[PATCH_SLOT_SIZE_BYTES];
    uint8_t *sector_buffer = NULL;
    const size_t erase_size = SPI_FLASH_SEC_SIZE;
    size_t phys_sector = 0u;
    size_t slot_offs = 0u;
    bool ok = false;

    if (phys == SPI_FLASH_CACHE2PHYS_FAIL) {
        return false;
    }

    phys_sector = phys & ~(erase_size - 1u);
    slot_offs = phys - phys_sector;

    board_pack_slot_word(oldv, expected_bytes);
    board_pack_slot_word(newv, new_bytes);

    sector_buffer = heap_caps_malloc(erase_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (sector_buffer == NULL) {
        return false;
    }

    if (esp_flash_read(esp_flash_default_chip, sector_buffer, phys_sector, erase_size) != ESP_OK) {
        goto cleanup;
    }

    if (memcmp(sector_buffer + slot_offs, expected_bytes, sizeof(expected_bytes)) != 0) {
        goto cleanup;
    }

    memcpy(sector_buffer + slot_offs, new_bytes, sizeof(new_bytes));

    if (esp_flash_erase_region(esp_flash_default_chip, phys_sector, erase_size) != ESP_OK) {
        goto cleanup;
    }

    if (esp_flash_write(esp_flash_default_chip, sector_buffer, phys_sector, erase_size) != ESP_OK) {
        goto cleanup;
    }

    s_last_patch_addr = addr;
    s_last_patch_size = PATCH_SLOT_SIZE_BYTES;
    board_code_sync();

    if (esp_flash_read(esp_flash_default_chip, raw_bytes, phys, sizeof(raw_bytes)) != ESP_OK) {
        goto cleanup;
    }

    if (memcmp(raw_bytes, new_bytes, sizeof(raw_bytes)) != 0) {
        goto cleanup;
    }

    ok = true;

cleanup:
    heap_caps_free(sector_buffer);
    return ok;
}

void board_code_sync(void)
{
    cache_hal_invalidate_addr(0x42000000u, 0x01000000u);

    __asm__ volatile("memw\n"
                     "isync\n"
                     ::: "memory");
}
