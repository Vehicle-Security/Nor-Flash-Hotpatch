/* Linker script for STM32F411CE hotpatch compare benchmark.
 *
 * STM32F411CE: 512 KB FLASH, 128 KB RAM (Cortex-M4F).
 * Based on the nRF52840 compare linker script with STM32 memory map.
 * Keeps the .hotpatch_page section for patch_slot and .hera_ram_text
 * for HERA scheme comparison.
 */

OUTPUT_FORMAT ("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size  = 0x200;   /* 512 bytes */
_Min_Stack_Size = 0x400;   /* 1 KB */

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)

        KEEP(*(.init))
        KEEP(*(.fini))

        /* .ctors */
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)

        /* .dtors */
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)

        *(.rodata*)

        KEEP(*(.eh_frame*))

        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* ------------------------------------------------------------------ */
    /* Hotpatch page: patch_slot lives here, page-aligned so it can be    */
    /* erased independently if needed.                                     */
    /* ------------------------------------------------------------------ */
    .hotpatch_page ALIGN(0x1000) :
    {
        __hotpatch_page_start__ = .;
        . = ALIGN(2);
        KEEP(*(.hotpatch_page.slot))
        KEEP(*(.hotpatch_page.entry))
        KEEP(*(.hotpatch_page))
        KEEP(*(.hotpatch_page.*))
        . = ALIGN(4);
        __hotpatch_page_end__ = .;
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;

    /* ------------------------------------------------------------------ */
    /* HERA RAM-text section: copied from FLASH to RAM at startup for the */
    /* HERA scheme comparison benchmark.                                   */
    /* ------------------------------------------------------------------ */
    .hera_ram_text :
    {
        . = ALIGN(4);
        __hera_ram_text_start__ = .;
        KEEP(*(.hera_ram_text))
        KEEP(*(.hera_ram_text.*))
        . = ALIGN(4);
        __hera_ram_text_end__ = .;
    } > RAM AT > FLASH

    __hera_ram_text_load_start__ = LOADADDR(.hera_ram_text);

    . = ALIGN(4);
    __etext = LOADADDR(.hera_ram_text) + SIZEOF(.hera_ram_text);
    _sidata = __etext;

    .data : AT (__etext)
    {
        . = ALIGN(4);
        __data_start__ = .;
        _sdata = .;
        *(vtable)
        *(.data*)

        . = ALIGN(4);

        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);

        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);

        . = ALIGN(4);

        /* fini data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);

        KEEP(*(.jcr*))
        . = ALIGN(4);

        __data_end__ = .;
        _edata = .;
    } > RAM

    .bss :
    {
        . = ALIGN(4);
        __bss_start__ = .;
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
        _ebss = .;
    } > RAM

    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        __HeapBase = .;
        __end__ = .;
        . = . + _Min_Heap_Size;
        __HeapLimit = .;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM

    __StackTop = _estack;
    __StackLimit = __StackTop - _Min_Stack_Size;
    PROVIDE(__stack = __StackTop);

    /* Discard debug sections for size reporting */
    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }

    ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed with stack")

    DataInitFlashUsed = __bss_start__ - __data_start__;
    CodeFlashUsed = __etext - ORIGIN(FLASH);
    TotalFlashUsed = CodeFlashUsed + DataInitFlashUsed;
    ASSERT(TotalFlashUsed <= LENGTH(FLASH), "region FLASH overflowed with .data and user data")
}
