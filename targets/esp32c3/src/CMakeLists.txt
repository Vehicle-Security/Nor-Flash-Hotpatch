file(GLOB MAIN_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.S")

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "../include"
    REQUIRES app_update esp_partition spi_flash driver esp_driver_uart
)

# Override the dangerous-write protection so we can hotpatch the running flash.
# This must be applied to the spi_flash component since that's where the check lives.
idf_build_set_property(COMPILE_OPTIONS
    "-UCONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS;-DCONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=1"
    APPEND)
