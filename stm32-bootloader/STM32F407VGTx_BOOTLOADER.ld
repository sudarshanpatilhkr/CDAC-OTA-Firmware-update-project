/*
 * STM32F407VGTx_BOOTLOADER.ld - Linker Script for Bootloader
 *
 * Bootloader occupies first 64KB of flash (Sectors 0-3)
 * Address: 0x08000000 - 0x0800FFFF
 */

/* Entry Point */
ENTRY(Reset_Handler)

/* Memory Definitions */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K    /* Bootloader flash */
    RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 128K   /* Main SRAM */
    CCMRAM (rw) : ORIGIN = 0x10000000, LENGTH = 64K    /* Core Coupled Memory */
}

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Minimum heap and stack sizes */
_Min_Heap_Size  = 0x200;    /* 512 bytes */
_Min_Stack_Size = 0x400;    /* 1KB */

SECTIONS
{
    /* Vector table and code */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    /* Initialized data */
    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    /* Uninitialized data */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    /* Heap and Stack */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM
}
