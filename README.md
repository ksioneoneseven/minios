# MiniOS

A minimal 32-bit operating system written from scratch in C and x86 assembly. MiniOS features a complete kernel with memory management, process scheduling, a virtual filesystem, ext2 read/write support, and a feature-rich shell.

## Features

### Kernel
- **Protected Mode**: Full 32-bit protected mode with GDT/IDT
- **Memory Management**: Physical memory manager, paging, kernel heap
- **Process Management**: Multi-process support with scheduler, signals (POSIX-style)
- **Interrupt Handling**: PIC, timer (100Hz), keyboard driver
- **System Calls**: 27+ syscalls for process control, file I/O, signals

### Filesystem
- **Virtual File System (VFS)**: Unified interface for multiple filesystems
- **RAMFS**: In-memory filesystem for root (`/`)
- **ext2 Support**: Full read/write support for ext2 partitions
  - File creation, deletion, reading, writing
  - Directory creation and traversal
  - Indirect block support (files up to 4GB)
  - Block/inode allocation and deallocation
  - Timestamps (atime, mtime, ctime, dtime)
- **Block Device Layer**: ATA/IDE PIO mode driver
- **MBR Partition Parsing**: Automatic partition detection
- **Auto-mount**: `/etc/fstab` support for automatic mounting at boot

### Shell
70+ built-in commands organized by category:

| Category | Commands |
|----------|----------|
| **File System** | ls, cat, touch, write, mkdir, rmdir, rm, cp, mv, pwd, cd, stat, head, tail, wc, mount, find, sort, rev |
| **Process** | ps, kill, sleep, top, nice, run, progs, time |
| **System** | mem, free, uptime, uname, date, hostname, dmesg, df, interrupts, lscpu, reboot |
| **User** | login, logout, su, passwd, useradd, userdel, groupadd, groups, chmod, chown, id, whoami |
| **Shell** | help, clear, echo, history, env, alias, export, set, which, type, seq |
| **Debug** | peek, poke, dump, heap, regs, gdt, idt, pages, stack |
| **Text** | hexdump, xxd, strings, grep, diff |
| **Apps** | nano (text editor), spreadsheet |
| **Misc** | color, version, about, credits, beep, banner, fortune, man |

### Applications
- **nano**: Full-featured text editor with cut/copy/paste, search, undo
- **spreadsheet**: Spreadsheet with formulas (SUM, basic arithmetic)

### Other Features
- User/group management with permissions
- System daemons (klogd, crond, etc.)
- Command history and tab completion
- Manual pages (`man <command>`)
- IPC: Pipes and signals

---

## Building MiniOS

### Prerequisites

MiniOS is built on **Windows with WSL (Windows Subsystem for Linux)**. You need:

1. **Windows 10/11** with WSL2 enabled
2. **Ubuntu** (or similar) installed in WSL
3. **QEMU** for running the OS

### Step 1: Install WSL and Ubuntu

```powershell
# In PowerShell (as Administrator)
wsl --install -d Ubuntu
```

Restart your computer, then open Ubuntu from the Start menu to complete setup.

### Step 2: Install Build Tools in WSL

```bash
# In Ubuntu/WSL terminal
sudo apt update
sudo apt install -y build-essential nasm grub-pc-bin xorriso mtools qemu-system-x86
```

Install the i686 cross-compiler:
```bash
sudo apt install -y gcc-multilib
# Or for a proper cross-compiler:
sudo apt install -y gcc-i686-linux-gnu
```

### Step 3: Clone/Copy the Repository

If you have the source on Windows at `C:\Users\YourName\Documents\minios`, it's accessible in WSL at:
```
/mnt/c/Users/YourName/Documents/minios
```

### Step 4: Build MiniOS

```bash
# In WSL
cd /mnt/c/Users/YourName/Documents/minios/build
make clean all
```

This produces:
- `miniOS.iso` - Bootable CD image
- `kernel.bin` - The kernel binary

---

## Running MiniOS

### Option 1: QEMU (Recommended)

#### Basic Boot (RAM only)
```bash
# In WSL
qemu-system-i386 -cdrom miniOS.iso -m 128M
```

#### With Hard Disk (ext2 persistence)
First, create and format a disk image:

```bash
# Create 64MB disk image
dd if=/dev/zero of=hda.img bs=1M count=64

# Partition with MBR (one Linux partition starting at sector 2048)
echo "label: dos
start=2048, type=83" | sfdisk hda.img

# Format the partition as ext2 (offset = 2048 * 512 = 1048576 bytes)
mkfs.ext2 -F -E offset=1048576 hda.img
```

Then run with the disk:
```bash
qemu-system-i386 -cdrom miniOS.iso -hda hda.img -m 128M
```

#### Ring 0 Mode (Kernel Shell)
To boot directly into the kernel shell (bypassing userland init):
```bash
qemu-system-i386 -cdrom miniOS.iso -hda hda.img -m 128M -append "ring0"
```

### Option 2: VirtualBox

1. Create a new VM:
   - Type: Other
   - Version: Other/Unknown
   - Memory: 128 MB
   - No hard disk (or create one)

2. Settings → Storage:
   - Add `miniOS.iso` to the IDE Controller as a CD

3. Settings → System:
   - Uncheck "Enable EFI"
   - Boot Order: Optical first

4. Start the VM

### Option 3: Real Hardware

Burn `miniOS.iso` to a CD or create a bootable USB with tools like Rufus. Boot from the media.

---

## Using MiniOS

### First Boot

After booting, you'll see the MiniOS banner and shell prompt:

```
=====================================
     Kernel loaded successfully!
=====================================

minios>
```

### Basic Commands

```bash
# List files
ls
ls /bin

# Navigate directories
cd /mnt
pwd

# Create and edit files
touch myfile.txt
nano myfile.txt
cat myfile.txt

# System info
uname
uptime
free
ps
```

### Using the ext2 Filesystem

If you booted with a hard disk, it's auto-mounted at `/mnt`:

```bash
# Check mount
df

# Navigate to mounted drive
cd /mnt
ls

# Create persistent files
mkdir documents
touch documents/notes.txt
nano documents/notes.txt

# Files persist across reboots!
```

### Manual Mounting

If auto-mount didn't work, mount manually:
```bash
mkdir /mnt
mount hd0p1 /mnt ext2
```

### Applications

```bash
# Text editor
nano filename.txt
# Ctrl+O: Save, Ctrl+X: Exit, Ctrl+K: Cut, Ctrl+U: Paste

# Spreadsheet
spreadsheet
# Arrows: Navigate, Enter: Edit, F2: Save, F3: Load, ESC: Exit
```

### Getting Help

```bash
# List all commands
help

# Manual pages
man ls
man nano
man mount
```

---

## Project Structure

```
minios/
├── boot/               # Bootloader and low-level assembly
│   ├── boot.asm        # Multiboot header, kernel entry
│   ├── gdt.asm         # GDT setup
│   ├── interrupts.asm  # ISR/IRQ stubs
│   └── switch.asm      # Context switching
├── kernel/
│   ├── kernel.c        # Main kernel entry point
│   ├── include/        # Header files
│   ├── drivers/        # Hardware drivers (VGA, keyboard, timer, ATA)
│   ├── lib/            # String, stdio, panic
│   ├── mm/             # Memory management (PMM, paging, heap)
│   ├── process/        # Process management, scheduler, signals
│   ├── syscall/        # System call handler
│   ├── fs/             # Filesystems (VFS, ramfs, ext2, blockdev)
│   ├── shell/          # Kernel shell
│   ├── apps/           # Applications (nano, spreadsheet)
│   ├── ipc/            # Inter-process communication (pipes)
│   └── interrupts/     # IDT, ISR, IRQ, PIC
├── userland/           # Userland programs
│   ├── libc/           # Minimal C library
│   └── bin/            # User programs (init, shell, hello)
├── build/
│   ├── Makefile        # Build system
│   └── grub.cfg        # GRUB configuration
└── hda.img             # Hard disk image (created separately)
```

---

## Configuration

### /etc/fstab

The kernel reads `/etc/fstab` at boot to auto-mount filesystems:

```
# device    mountpoint    fstype    options
hd0p1       /mnt          ext2      defaults
```

Edit with `nano /etc/fstab` (changes don't persist in ramfs).

---

## Development

### Adding a New Shell Command

1. Add forward declaration in `kernel/shell/shell.c`:
   ```c
   static int cmd_mycommand(int argc, char* argv[]);
   ```

2. Register in `shell_init()`:
   ```c
   shell_register_command("mycommand", "Description", cmd_mycommand);
   ```

3. Implement the command:
   ```c
   static int cmd_mycommand(int argc, char* argv[]) {
       vga_puts("Hello from mycommand!\n");
       return 0;
   }
   ```

4. Add a man page entry in `cmd_man()`.

### Adding a System Call

1. Define syscall number in `kernel/include/syscall.h`
2. Add handler in `kernel/syscall/syscall.c`
3. Add userland wrapper in `userland/libc/`

---

## Technical Details

- **Architecture**: i386 (32-bit x86)
- **Boot**: Multiboot specification via GRUB
- **Memory**: Up to 4GB addressable (paging enabled)
- **Timer**: 100 Hz PIT
- **Disk**: ATA PIO mode
- **Filesystem**: ext2 with 1KB or 4KB blocks

---

## License

This project is for educational purposes.

---

## Acknowledgments

MiniOS was developed as a learning project to understand operating system internals, from bootloaders to filesystems.
