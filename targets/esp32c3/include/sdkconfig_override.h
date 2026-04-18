/* Override ESP-IDF's dangerous write protection.
 * MorphPatch needs to write to the running flash partition. */
#ifdef CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS
#undef CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS
#endif
#ifndef CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED
#define CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED 1
#endif
