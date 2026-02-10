/* MiniOS User Program Linker Script */

/* User programs start at 0x08048000 (traditional Linux user base) */
/* We use 0x40000000 (1GB) for simplicity */

ENTRY(_start)

SECTIONS
{
    . = 0x40000000;
    
    .text ALIGN(4K) :
    {
        *(.text)
        *(.text.*)
    }
    
    .rodata ALIGN(4K) :
    {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
    }
    
    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }
    
    _end = .;
    
    /* Heap starts after BSS */
    __heap_start = .;
    
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}

