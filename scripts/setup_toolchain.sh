#!/bin/bash
# MiniOS Toolchain Setup Script
# Run this in WSL (Ubuntu) or a Linux VM
# Usage: chmod +x setup_toolchain.sh && ./setup_toolchain.sh

set -e

echo "=========================================="
echo "  MiniOS Toolchain Setup"
echo "  Target: i686-elf (32-bit x86)"
echo "=========================================="

# Configuration
export PREFIX="$HOME/opt/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

BINUTILS_VERSION="2.41"
GCC_VERSION="13.2.0"

# Check if already installed
if command -v i686-elf-gcc &> /dev/null; then
    echo "[OK] Cross-compiler already installed!"
    i686-elf-gcc --version | head -1
    exit 0
fi

echo "[1/6] Installing dependencies..."
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    bison \
    flex \
    libgmp3-dev \
    libmpc-dev \
    libmpfr-dev \
    texinfo \
    nasm \
    grub-pc-bin \
    grub-common \
    xorriso \
    qemu-system-x86 \
    gdb

echo "[2/6] Creating build directories..."
mkdir -p "$PREFIX"
mkdir -p "$HOME/src"
cd "$HOME/src"

echo "[3/6] Downloading binutils..."
if [ ! -f "binutils-${BINUTILS_VERSION}.tar.xz" ]; then
    wget "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.xz"
fi
tar -xf "binutils-${BINUTILS_VERSION}.tar.xz"

echo "[4/6] Building binutils..."
mkdir -p build-binutils
cd build-binutils
../binutils-${BINUTILS_VERSION}/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror
make -j$(nproc)
make install
cd ..

echo "[5/6] Downloading GCC..."
if [ ! -f "gcc-${GCC_VERSION}.tar.xz" ]; then
    wget "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz"
fi
tar -xf "gcc-${GCC_VERSION}.tar.xz"

echo "[6/6] Building GCC..."
mkdir -p build-gcc
cd build-gcc
../gcc-${GCC_VERSION}/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c \
    --without-headers
make -j$(nproc) all-gcc
make -j$(nproc) all-target-libgcc
make install-gcc
make install-target-libgcc
cd ..

echo ""
echo "=========================================="
echo "  Toolchain installed successfully!"
echo "=========================================="
echo ""
echo "Add this to your ~/.bashrc:"
echo "  export PATH=\"\$HOME/opt/cross/bin:\$PATH\""
echo ""
echo "Then run: source ~/.bashrc"
echo ""
echo "Verify with: i686-elf-gcc --version"

