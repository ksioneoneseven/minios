# MiniOS Development Journal

## 2026-01-31 - Phase 0: Project Initialization

### Goals Today
- [x] Initialize project structure
- [x] Create development journal
- [x] Set up architecture documentation
- [x] Create toolchain setup script for WSL/Linux
- [x] Create build system skeleton
- [ ] Prepare for Phase 1.1 (boot.asm)

### Progress
- Project started from scratch
- Development environment: Windows with WSL/Linux VM
- Target architecture: x86 32-bit
- Bootloader: GRUB 2 (multiboot specification)

### Environment Setup
- **Host OS**: Windows
- **Build Environment**: WSL (Ubuntu recommended) or Linux VM
- **Required Tools**: 
  - i686-elf-gcc (cross-compiler)
  - NASM (assembler)
  - GRUB 2 (grub-mkrescue)
  - QEMU (testing)
  - make, binutils

### Project Structure Created
```
miniOS/
├── journal/progress.md      [CREATED]
├── docs/ARCHITECTURE.md     [CREATED]
├── scripts/
│   ├── setup_toolchain.sh   [CREATED]
│   └── run_qemu.sh          [CREATED]
├── build/
│   ├── Makefile             [CREATED]
│   ├── kernel.ld            [CREATED]
│   └── iso/boot/grub/grub.cfg [CREATED]
├── boot/                    [PENDING - Phase 1.1]
├── kernel/                  [PENDING - Phase 1.2+]
└── userland/                [PENDING - Phase 7]
```

### Next Steps
1. Run setup_toolchain.sh in WSL/Linux to install cross-compiler
2. Begin Phase 1.1: Create boot.asm (multiboot bootloader)
3. Create minimal kernel.c to verify boot sequence
4. Test with QEMU

### Notes
- Using multiboot specification for GRUB compatibility
- Initial kernel will be loaded at 1MB (0x100000)
- Stack size: 16KB
- VGA text mode buffer at 0xB8000

---

## 2026-01-30 - Phase 1.1: Boot Sequence ✅ COMPLETE

### Goals
- [x] Create multiboot-compliant bootloader
- [x] Implement VGA text mode driver
- [x] Boot kernel in QEMU via GRUB
- [x] Display boot banner with colors
- [x] Verify multiboot magic number

### Accomplishments
**MiniOS successfully boots!** 🎉

- Created `boot/boot.asm` - Multiboot entry point with 16KB stack
- Created `kernel/kernel.c` - Main kernel entry with boot banner
- Created `kernel/drivers/vga.c` - VGA text driver (80x25, 16 colors, scrolling)
- Created `kernel/lib/string.c` - String/memory functions (memset, memcpy, strlen, etc.)
- Created header files: `types.h`, `multiboot.h`, `vga.h`, `string.h`

### Test Results
- **Bootloader**: GRUB 2.12-1ubuntu7.3
- **Multiboot magic**: 0x2BADB002 ✅
- **Lower memory**: 0x27F KB (639 KB)
- **Upper memory**: 0x1FB80 KB (~130,944 KB)
- **Total memory**: ~127 MB
- **No triple-fault** - stable boot achieved!

### Files Created
```
boot/boot.asm              - Multiboot bootloader entry
kernel/kernel.c            - Main kernel entry point
kernel/drivers/vga.c       - VGA text mode driver
kernel/lib/string.c        - String/memory functions
kernel/include/types.h     - Fixed-width integer types
kernel/include/multiboot.h - Multiboot structures
kernel/include/vga.h       - VGA driver interface
kernel/include/string.h    - String function prototypes
```

### Next Steps
- ✅ Phase 1.3 Complete!

---

## 2026-01-30 - Phase 1.3: GDT & IDT Setup ✅ COMPLETE

### Goals
- [x] Create Global Descriptor Table (GDT)
- [x] Create Interrupt Descriptor Table (IDT)
- [x] Implement CPU exception handlers (ISR 0-31)
- [x] Implement hardware interrupt handlers (IRQ 0-15)
- [x] Configure 8259 PIC (remap IRQs to INT 32-47)
- [x] Enable interrupts without crashing

### Files Created
```
boot/gdt.asm                    - GDT/TSS flush assembly
boot/interrupts.asm             - ISR/IRQ stubs (saves state, calls C)
kernel/include/gdt.h            - GDT structures and constants
kernel/include/idt.h            - IDT structures and registers_t
kernel/include/isr.h            - ISR/IRQ declarations
kernel/include/pic.h            - 8259 PIC interface
kernel/include/io.h             - I/O port functions (inb/outb)
kernel/cpu/gdt.c                - GDT initialization (5 segments + TSS)
kernel/interrupts/idt.c         - IDT initialization (256 entries)
kernel/interrupts/isr.c         - Exception handler with register dump
kernel/interrupts/irq.c         - Hardware interrupt dispatcher
kernel/interrupts/pic.c         - PIC initialization and EOI
```

### Technical Details
- **GDT Entries**: Null, Kernel Code, Kernel Data, User Code, User Data, TSS
- **IDT Entries**: 0-31 (CPU exceptions), 32-47 (hardware IRQs)
- **PIC Remapping**: IRQ 0-7 → INT 32-39, IRQ 8-15 → INT 40-47
- **Exception Handler**: Displays register dump on kernel panic

### Success Criteria Met
- [x] System boots with GDT/IDT loaded
- [x] Interrupts enabled without triple-fault
- [x] No crashes when idle

### Next Steps
- ✅ Phase 1.4 Complete!

---

## 2026-01-31 - Phase 1.4: Kernel Library ✅ COMPLETE

### Goals
- [x] Create printk (kernel printf) with format specifiers
- [x] Create sprintf/snprintf for string formatting
- [x] Create kernel_panic() with register dump
- [x] Create keyboard driver (PS/2, IRQ1)
- [x] Create timer driver (PIT, IRQ0)
- [x] Test keyboard input echo

### Files Created
```
kernel/include/stdio.h          - printf-like function declarations
kernel/include/panic.h          - Panic and assertion macros
kernel/include/keyboard.h       - Keyboard driver interface
kernel/include/timer.h          - Timer driver interface
kernel/lib/stdio.c              - printk, sprintf, snprintf implementation
kernel/lib/panic.c              - kernel_panic with colored output
kernel/drivers/keyboard.c       - PS/2 keyboard with scancode translation
kernel/drivers/timer.c          - PIT timer at configurable frequency
```

### Technical Details
- **printk**: Supports %d, %i, %u, %x, %X, %c, %s, %p, %% with width/padding
- **Keyboard**: US layout, Shift/Ctrl/Alt/CapsLock support, circular buffer
- **Timer**: 100 Hz tick rate, sleep functions available
- **Variadic args**: Using GCC builtins (__builtin_va_list) for freestanding

### Success Criteria Met
- [x] printk() works with format specifiers
- [x] Keyboard input echoes to screen
- [x] Timer interrupts working (100 Hz)
- [x] Can type and see characters appear

### Next Steps
1. **Phase 2.1**: Physical memory manager (bitmap allocator)
2. **Phase 2.2**: Virtual memory (paging)
3. **Phase 2.3**: Kernel heap (kmalloc/kfree)

---

