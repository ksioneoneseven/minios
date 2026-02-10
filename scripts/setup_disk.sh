#!/bin/bash
set -e

cd /mnt/c/Users/rstacy/Documents/minios-main/minios-main

echo "=== Step 1: Recreate disk image ==="
dd if=/dev/zero of=hda.img bs=1M count=64

echo "=== Step 2: Partition with MBR ==="
printf "label: dos\nstart=2048, type=83\n" | sfdisk --force hda.img

echo "=== Step 3: Verify partition ==="
sfdisk -l hda.img

echo "=== Step 4: Format ext2 ==="
mkfs.ext2 -F -E offset=1048576 hda.img

echo "=== Step 5: Mount and copy hills.bmp ==="
LOOP=$(losetup -f --show -o 1048576 hda.img)
mkdir -p /tmp/minios_disk
mount "$LOOP" /tmp/minios_disk
cp hills.bmp /tmp/minios_disk/
ls -l /tmp/minios_disk/
sync
umount /tmp/minios_disk
losetup -d "$LOOP"

echo "=== DONE: hda.img ready with hills.bmp ==="
