# MiniOS Architecture

## Overview

MiniOS is a minimal Unix-like operating system for x86 (32-bit) architecture. It boots via GRUB 2, provides a functional shell, and supports basic utilities.

## System Layers

```
┌─────────────────────────────────────────────────────────┐
│                    User Applications                     │
│              (shell, ls, cat, echo, ps)                 │
├─────────────────────────────────────────────────────────┤
│                    User-space libc                       │
│           (printf, malloc, syscall wrappers)            │
├─────────────────────────────────────────────────────────┤
│                   System Call Interface                  │
│                      (INT 0x80)                         │
├─────────────────────────────────────────────────────────┤
│                        Kernel                            │
│  ┌─────────┬──────────┬─────────┬──────────┬─────────┐ │
│  │ Process │  Memory  │   VFS   │ Drivers  │  IRQ/   │ │
│  │ Manager │ Manager  │  Layer  │          │  Timer  │ │
│  └─────────┴──────────┴─────────┴──────────┴─────────┘ │
├─────────────────────────────────────────────────────────┤
│                      Hardware                            │
│         (CPU, RAM, VGA, Keyboard, Disk)                 │
└─────────────────────────────────────────────────────────┘
```

## Boot Sequence

1. **BIOS/UEFI** → Loads GRUB from boot device
2. **GRUB** → Loads kernel.bin via multiboot
3. **boot.asm** → Sets up stack, GDT, jumps to C
4. **kernel_main()** → Initializes all subsystems
5. **init process** → First userspace process (PID 1)
6. **shell** → Spawned by init

## Memory Layout

### Physical Memory
```
0x00000000 - 0x000FFFFF : Reserved (BIOS, VGA, etc.)
0x00100000 - 0x003FFFFF : Kernel code and data (~3MB)
0x00400000 - 0x007FFFFF : Kernel heap (~4MB)
0x00800000 - ...        : User processes / free memory
```

### Virtual Memory (with paging enabled)
```
0x00000000 - 0xBFFFFFFF : User space (3GB)
0xC0000000 - 0xFFFFFFFF : Kernel space (1GB, higher-half)
```

## Key Components

### 1. Boot (boot/)
- **boot.asm**: Multiboot header, stack setup, GDT load, jump to C
- **gdt.asm**: Global Descriptor Table (5 entries)
- **interrupts.asm**: IDT setup, ISR stubs

### 2. Kernel Core (kernel/)
- **kernel.c**: Main entry point, subsystem initialization
- **lib/**: String functions, printk, panic handler

### 3. Memory Management (kernel/mm/)
- **pmm.c**: Physical page allocator (bitmap)
- **paging.c**: Virtual memory, paging, page fault handler
- **heap.c**: kmalloc/kfree for kernel

### 4. Interrupts (kernel/interrupts/)
- **idt.c**: Interrupt Descriptor Table
- **isr.c**: Exception handlers (0-31)
- **irq.c**: Hardware interrupts (32-47)
- **pic.c**: 8259 PIC configuration

### 5. Drivers (kernel/drivers/)
- **vga.c**: Text mode display (80x25)
- **keyboard.c**: PS/2 keyboard input
- **timer.c**: PIT at 100Hz

### 6. Filesystem (kernel/fs/)
- **vfs.c**: Virtual filesystem abstraction
- **ramfs.c**: In-memory filesystem used as root

### 7. Process Management (kernel/process/)
- **process.c**: PCB, process table
- **scheduler.c**: Round-robin, 10ms quantum
- **switch.asm**: Register save/restore
- **syscall.c**: INT 0x80 dispatcher

### 8. Userland (userland/)
- **libc/**: Minimal C library
- **bin/**: init, shell, utilities

## System Calls

| # | Name | Description |
|---|------|-------------|
| 1 | exit | Terminate process |
| 2 | fork | Create child process |
| 3 | read | Read from file descriptor |
| 4 | write | Write to file descriptor |
| 5 | open | Open file |
| 6 | close | Close file descriptor |
| 7 | waitpid | Wait for child |
| 8 | exec | Execute program |
| 9 | getpid | Get process ID |
| 10 | sbrk | Extend heap |

## Design Decisions

1. **32-bit x86**: Simpler than x86_64, well-documented
2. **Multiboot**: Standard interface, GRUB handles boot complexity
3. **Monolithic kernel**: All drivers in kernel space (simpler)
4. **Round-robin scheduler**: Fair, simple to implement
5. **InitRD**: No complex disk filesystem needed initially
6. **Higher-half kernel**: Clean separation of user/kernel space

