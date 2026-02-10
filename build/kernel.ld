/*
 * MiniOS Kernel Linker Script
 * 
 * Memory Layout:
 * - Kernel loaded at 1MB (0x100000) by GRUB
 * - Sections: .multiboot, .text, .rodata, .data, .bss
 */

ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 1MB - standard location for multiboot kernels */
    . = 1M;

    /* Multiboot header must be first (within first 8KB) */
    .multiboot ALIGN(4K) :
    {
        *(.multiboot)
    }

    /* Code section */
    .text ALIGN(4K) :
    {
        *(.text)
    }

    /* Read-only data */
    .rodata ALIGN(4K) :
    {
        *(.rodata)
    }

    /* Initialized data */
    .data ALIGN(4K) :
    {
        *(.data)
    }

    /* Uninitialized data (zeroed by loader) */
    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss)
    }

    /* End of kernel - used for heap start */
    _kernel_end = .;
}

