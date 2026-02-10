# MiniOS Userland Makefile
# Builds the C library and user programs

# Cross-compiler
CC = /home/rstacy/opt/cross/bin/i686-elf-gcc
AS = nasm
AR = /home/rstacy/opt/cross/bin/i686-elf-ar
LD = /home/rstacy/opt/cross/bin/i686-elf-ld

# Directories
LIBC_DIR = libc
BIN_DIR = bin
BUILD_DIR = ../build/userland

# Compiler flags
CFLAGS = -ffreestanding -O2 -Wall -Wextra -nostdlib -nostdinc \
         -fno-builtin -fno-stack-protector -m32

# Assembler flags
ASFLAGS = -f elf32

# Linker flags
LDFLAGS = -nostdlib -static

# Libc source files
LIBC_C_SOURCES = $(LIBC_DIR)/string.c \
                 $(LIBC_DIR)/stdio.c \
                 $(LIBC_DIR)/stdlib.c

LIBC_ASM_SOURCES = $(LIBC_DIR)/start.asm

# User programs
USER_PROGRAMS = hello init shell

# Object files
LIBC_C_OBJECTS = $(patsubst $(LIBC_DIR)/%.c,$(BUILD_DIR)/libc/%.o,$(LIBC_C_SOURCES))
LIBC_ASM_OBJECTS = $(patsubst $(LIBC_DIR)/%.asm,$(BUILD_DIR)/libc/%.o,$(LIBC_ASM_SOURCES))
LIBC_OBJECTS = $(LIBC_ASM_OBJECTS) $(LIBC_C_OBJECTS)

# User program binaries
USER_BINARIES = $(patsubst %,$(BUILD_DIR)/bin/%,$(USER_PROGRAMS))

# Default target
.PHONY: all clean

all: dirs libc programs
	@echo "[OK]  Userland build complete!"

dirs:
	@mkdir -p $(BUILD_DIR)/libc
	@mkdir -p $(BUILD_DIR)/bin

# Build libc
libc: $(BUILD_DIR)/libc.a

$(BUILD_DIR)/libc.a: $(LIBC_OBJECTS)
	@echo "[AR]  Creating libc.a..."
	@$(AR) rcs $@ $^

# Compile libc C files
$(BUILD_DIR)/libc/%.o: $(LIBC_DIR)/%.c
	@echo "[CC]  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Assemble libc ASM files
$(BUILD_DIR)/libc/%.o: $(LIBC_DIR)/%.asm
	@echo "[ASM] $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Build user programs
programs: $(USER_BINARIES)

$(BUILD_DIR)/bin/%: $(BIN_DIR)/%.c $(BUILD_DIR)/libc.a
	@echo "[CC]  $<"
	@$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/bin/$*.o
	@echo "[LD]  $@"
	@$(LD) $(LDFLAGS) -T user.ld -o $@ $(BUILD_DIR)/libc/start.o $(BUILD_DIR)/bin/$*.o $(BUILD_DIR)/libc.a

clean:
	@echo "[CLEAN] Removing userland build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "[OK]  Clean complete!"

