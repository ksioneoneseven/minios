#!/bin/bash
# MiniOS QEMU Launch Script
# Usage: ./scripts/run_qemu.sh [debug]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ISO_PATH="$PROJECT_DIR/miniOS.iso"

# Check if ISO exists
if [ ! -f "$ISO_PATH" ]; then
    echo "Error: miniOS.iso not found!"
    echo "Run 'make' first to build the OS."
    exit 1
fi

# Common QEMU options
QEMU_OPTS="-cdrom $ISO_PATH"
QEMU_OPTS="$QEMU_OPTS -m 128M"
QEMU_OPTS="$QEMU_OPTS -serial stdio"
QEMU_OPTS="$QEMU_OPTS -vga std"

# Debug mode
if [ "$1" == "debug" ]; then
    echo "Starting QEMU in debug mode..."
    echo "Connect GDB with: target remote localhost:1234"
    QEMU_OPTS="$QEMU_OPTS -s -S"
fi

echo "Launching MiniOS..."
qemu-system-i386 $QEMU_OPTS

