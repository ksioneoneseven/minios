# MiniOS Master Makefile
# Usage: make [all|kernel|clean|run|debug]

# Cross-compiler toolchain (use full path for WSL)
CROSS_PREFIX = /home/rstacy/opt/cross/bin/
AS = nasm
CC = $(CROSS_PREFIX)i686-elf-gcc
LD = $(CROSS_PREFIX)i686-elf-ld

# Directories
ROOT_DIR := $(shell dirname $(CURDIR))
BUILD_DIR := $(CURDIR)
BOOT_DIR := $(ROOT_DIR)/boot
KERNEL_DIR := $(ROOT_DIR)/kernel

# Compiler flags
ASFLAGS = -f elf32
CFLAGS = -m32 -std=gnu99 -ffreestanding -nostdlib -nostdinc \
         -fno-builtin -fno-stack-protector \
         -Wall -Wextra -Werror -O2 \
         -I$(KERNEL_DIR)/include
LDFLAGS = -T $(BUILD_DIR)/kernel.ld -nostdlib

# Source files (will grow as we add components)
ASM_SOURCES = $(BOOT_DIR)/boot.asm \
              $(BOOT_DIR)/gdt.asm \
              $(BOOT_DIR)/interrupts.asm \
              $(BOOT_DIR)/switch.asm
C_SOURCES = $(KERNEL_DIR)/kernel.c \
            $(KERNEL_DIR)/drivers/vga.c \
            $(KERNEL_DIR)/drivers/keyboard.c \
            $(KERNEL_DIR)/drivers/timer.c \
            $(KERNEL_DIR)/drivers/ata.c \
            $(KERNEL_DIR)/drivers/vesa.c \
            $(KERNEL_DIR)/drivers/mouse.c \
            $(KERNEL_DIR)/drivers/serial.c \
            $(KERNEL_DIR)/drivers/rtc.c \
            $(KERNEL_DIR)/lib/string.c \
            $(KERNEL_DIR)/lib/stdio.c \
            $(KERNEL_DIR)/lib/panic.c \
            $(KERNEL_DIR)/cpu/gdt.c \
            $(KERNEL_DIR)/mm/pmm.c \
            $(KERNEL_DIR)/mm/paging.c \
            $(KERNEL_DIR)/mm/heap.c \
            $(KERNEL_DIR)/mm/uaccess.c \
            $(KERNEL_DIR)/mm/pagefault.c \
            $(KERNEL_DIR)/process/process.c \
            $(KERNEL_DIR)/process/scheduler.c \
            $(KERNEL_DIR)/process/signal.c \
            $(KERNEL_DIR)/ipc/pipe.c \
            $(KERNEL_DIR)/syscall/syscall.c \
            $(KERNEL_DIR)/fs/vfs.c \
            $(KERNEL_DIR)/fs/ramfs.c \
            $(KERNEL_DIR)/fs/blockdev.c \
            $(KERNEL_DIR)/fs/ext2.c \
            $(KERNEL_DIR)/fs/conf.c \
            $(KERNEL_DIR)/shell/shell.c \
            $(KERNEL_DIR)/loader/loader.c \
            $(KERNEL_DIR)/loader/elf.c \
            $(KERNEL_DIR)/user/user.c \
            $(KERNEL_DIR)/daemon/daemon.c \
            $(KERNEL_DIR)/apps/spreadsheet.c \
            $(KERNEL_DIR)/apps/nano.c \
            $(KERNEL_DIR)/apps/basic.c \
            $(KERNEL_DIR)/xgui/font.c \
            $(KERNEL_DIR)/xgui/display.c \
            $(KERNEL_DIR)/xgui/event.c \
            $(KERNEL_DIR)/xgui/wm.c \
            $(KERNEL_DIR)/xgui/desktop.c \
            $(KERNEL_DIR)/xgui/widget.c \
            $(KERNEL_DIR)/xgui/xgui.c \
            $(KERNEL_DIR)/xgui/theme.c \
            $(KERNEL_DIR)/xgui/clipboard.c \
            $(KERNEL_DIR)/xgui/contextmenu.c \
            $(KERNEL_DIR)/xgui/apps/calculator.c \
            $(KERNEL_DIR)/xgui/apps/controlpanel.c \
            $(KERNEL_DIR)/xgui/apps/gui_editor.c \
            $(KERNEL_DIR)/xgui/apps/gui_spreadsheet.c \
            $(KERNEL_DIR)/xgui/apps/about.c \
            $(KERNEL_DIR)/xgui/apps/notepad.c \
            $(KERNEL_DIR)/xgui/apps/paint.c \
            $(KERNEL_DIR)/xgui/apps/explorer.c \
            $(KERNEL_DIR)/xgui/apps/terminal.c \
            $(KERNEL_DIR)/xgui/apps/taskmgr.c \
            $(KERNEL_DIR)/xgui/apps/diskutil.c \
            $(KERNEL_DIR)/xgui/apps/clocksettings.c \
            $(KERNEL_DIR)/xgui/apps/analogclock.c \
            $(KERNEL_DIR)/xgui/apps/calendar.c \
            $(KERNEL_DIR)/xgui/apps/skigame.c \
            $(KERNEL_DIR)/xgui/apps/flappycat.c \
            $(KERNEL_DIR)/interrupts/idt.c \
            $(KERNEL_DIR)/interrupts/isr.c \
            $(KERNEL_DIR)/interrupts/irq.c \
            $(KERNEL_DIR)/interrupts/pic.c

# Object files
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)
C_OBJECTS = $(C_SOURCES:.c=.o)
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Output files
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
KERNEL_GUI_BIN = $(BUILD_DIR)/kernel_gui.bin
ISO_FILE = $(ROOT_DIR)/miniOS.iso
ISO_DIR = $(BUILD_DIR)/iso

# GUI boot assembly
BOOT_GUI_ASM = $(BOOT_DIR)/boot_gui.asm
BOOT_GUI_OBJ = $(BOOT_DIR)/boot_gui.o

# Userland directory
USERLAND_DIR = $(ROOT_DIR)/userland
USERLAND_BUILD = $(BUILD_DIR)/userland
SHELL_ELF = $(USERLAND_BUILD)/bin/shell
SHELL_OBJ = $(BUILD_DIR)/shell_embed.o
INIT_ELF = $(USERLAND_BUILD)/bin/init
INIT_OBJ = $(BUILD_DIR)/init_embed.o

# Default target
all: userland $(ISO_FILE)

# Build userland first
userland:
	@echo "[USERLAND] Building userland programs..."
	@$(MAKE) -C $(USERLAND_DIR) all

# Convert userland ELFs to embeddable objects
$(SHELL_OBJ): $(SHELL_ELF)
	@echo "[EMBED] Embedding shell binary..."
	@cd $(USERLAND_BUILD)/bin && $(CROSS_PREFIX)i686-elf-objcopy -I binary -O elf32-i386 \
		-B i386 --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		shell shell_embed.o
	@mv $(USERLAND_BUILD)/bin/shell_embed.o $(SHELL_OBJ)

$(INIT_OBJ): $(INIT_ELF)
	@echo "[EMBED] Embedding init binary..."
	@cd $(USERLAND_BUILD)/bin && $(CROSS_PREFIX)i686-elf-objcopy -I binary -O elf32-i386 \
		-B i386 --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		init init_embed.o
	@mv $(USERLAND_BUILD)/bin/init_embed.o $(INIT_OBJ)

# Compile assembly files
%.o: %.asm
	@echo "[ASM] $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Compile C files
%.o: %.c
	@echo "[CC]  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Link kernel (with embedded userland)
$(KERNEL_BIN): $(OBJECTS) $(SHELL_OBJ) $(INIT_OBJ)
	@echo "[LD]  $@"
	@$(LD) $(LDFLAGS) -o $@ $^

# Build GUI boot object
$(BOOT_GUI_OBJ): $(BOOT_GUI_ASM)
	@echo "[ASM] $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Link GUI kernel (replace boot.o with boot_gui.o)
$(KERNEL_GUI_BIN): $(BOOT_GUI_OBJ) $(BOOT_DIR)/gdt.o $(BOOT_DIR)/interrupts.o $(BOOT_DIR)/switch.o $(C_OBJECTS) $(SHELL_OBJ) $(INIT_OBJ)
	@echo "[LD]  $@"
	@$(LD) $(LDFLAGS) -o $@ $^

# Create bootable ISO
$(ISO_FILE): $(KERNEL_BIN) $(KERNEL_GUI_BIN)
	@echo "[ISO] Creating bootable ISO..."
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL_BIN) $(ISO_DIR)/boot/
	@cp $(KERNEL_GUI_BIN) $(ISO_DIR)/boot/
	@grub-mkrescue -o $(ISO_FILE) $(ISO_DIR) 2>/dev/null
	@echo "[OK]  $(ISO_FILE) created!"

# Run in QEMU
run: $(ISO_FILE)
	@echo "Launching MiniOS in QEMU..."
	@qemu-system-i386 -cdrom $(ISO_FILE) -m 128M -serial stdio

# Debug with GDB
debug: $(ISO_FILE)
	@echo "Starting QEMU in debug mode (port 1234)..."
	@echo "Connect with: gdb -ex 'target remote localhost:1234'"
	@qemu-system-i386 -cdrom $(ISO_FILE) -m 128M -serial stdio -s -S

# Clean build artifacts
clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -f $(OBJECTS)
	@rm -f $(KERNEL_BIN)
	@rm -f $(KERNEL_GUI_BIN)
	@rm -f $(BOOT_GUI_OBJ)
	@rm -f $(SHELL_OBJ)
	@rm -f $(INIT_OBJ)
	@rm -rf $(ISO_DIR)/boot/kernel.bin
	@rm -rf $(ISO_DIR)/boot/kernel_gui.bin
	@rm -f $(ISO_FILE)
	@$(MAKE) -C $(USERLAND_DIR) clean
	@echo "[OK]  Clean complete!"

# Show help
help:
	@echo "MiniOS Build System"
	@echo "==================="
	@echo "  make all    - Build bootable ISO"
	@echo "  make run    - Build and run in QEMU"
	@echo "  make debug  - Build and run with GDB support"
	@echo "  make clean  - Remove build artifacts"
	@echo ""
	@echo "Prerequisites:"
	@echo "  - i686-elf-gcc cross-compiler"
	@echo "  - nasm assembler"
	@echo "  - grub-mkrescue"
	@echo "  - qemu-system-i386"

.PHONY: all run debug clean help userland

