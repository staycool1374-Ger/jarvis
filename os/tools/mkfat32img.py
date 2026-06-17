#!/usr/bin/env python3
"""Generate a minimal FAT32 disk image with known test content."""

import struct
import sys

BLOCK_SIZE = 512
PART_LBA = 2048  # 1 MiB alignment

def build_mbr(part_lba, part_sectors):
    mbr = bytearray(BLOCK_SIZE)
    # Partition entry at offset 446
    mbr[446 + 0] = 0x80  # bootable
    mbr[446 + 1] = 0x00  # CHS start head
    mbr[446 + 2] = 0x02  # CHS start sector/cylinder
    mbr[446 + 3] = 0x00  # CHS start cylinder
    mbr[446 + 4] = 0x0C  # partition type: FAT32 LBA
    mbr[446 + 5] = 0x00  # CHS end head
    mbr[446 + 6] = 0x00  # CHS end sector/cylinder
    mbr[446 + 7] = 0x00  # CHS end cylinder
    struct.pack_into('<I', mbr, 446 + 8, part_lba)
    struct.pack_into('<I', mbr, 446 + 12, part_sectors)
    mbr[510] = 0x55
    mbr[511] = 0xAA
    return mbr


def build_bpb(sectors_per_cluster=1, reserved_sectors=32, fat_count=2,
              fat_size=64, root_cluster=2, volume_label=b'JARVIS_TEST',
              total_sectors=288):
    """Build a FAT32 BPB boot sector."""
    bpb = bytearray(BLOCK_SIZE)
    # Jump code (3 bytes)
    bpb[0] = 0xEB
    bpb[1] = 0x58
    bpb[2] = 0x90
    # OEM name (8 bytes)
    bpb[3:11] = b'JARVIS   '
    # Bytes per sector
    struct.pack_into('<H', bpb, 11, BLOCK_SIZE)
    # Sectors per cluster
    bpb[13] = sectors_per_cluster
    # Reserved sectors
    struct.pack_into('<H', bpb, 14, reserved_sectors)
    # Number of FATs
    bpb[16] = fat_count
    # Root entry count (0 for FAT32)
    struct.pack_into('<H', bpb, 17, 0)
    # Total sectors 16-bit (0 for FAT32)
    struct.pack_into('<H', bpb, 19, 0)
    # Media descriptor
    bpb[21] = 0xF8
    # Sectors per FAT 16-bit (0 for FAT32)
    struct.pack_into('<H', bpb, 22, 0)
    # Sectors per track
    struct.pack_into('<H', bpb, 24, 63)
    # Number of heads
    struct.pack_into('<H', bpb, 26, 255)
    # Hidden sectors (partition LBA)
    struct.pack_into('<I', bpb, 28, PART_LBA)
    # Total sectors 32-bit
    struct.pack_into('<I', bpb, 32, total_sectors)
    # Sectors per FAT 32-bit
    struct.pack_into('<I', bpb, 36, fat_size)
    # Mirror flags
    struct.pack_into('<H', bpb, 40, 0)
    # Filesystem version
    struct.pack_into('<H', bpb, 42, 0)
    # Root cluster
    struct.pack_into('<I', bpb, 44, root_cluster)
    # FSInfo sector
    struct.pack_into('<H', bpb, 48, 1)
    # Backup boot sector
    struct.pack_into('<H', bpb, 50, 3)
    # Reserved (52-63)
    # Physical drive number
    bpb[64] = 0x80
    # Extended boot signature
    bpb[66] = 0x29
    # Volume serial number
    struct.pack_into('<I', bpb, 67, 0x12345678)
    # Volume label (11 bytes)
    if len(volume_label) < 11:
        volume_label = volume_label + b' ' * (11 - len(volume_label))
    bpb[71:82] = volume_label[:11]
    # Filesystem type
    bpb[82:90] = b'FAT32   '
    # Boot code
    bpb[90:510] = b'\x00' * 420
    # Signature
    bpb[510] = 0x55
    bpb[511] = 0xAA
    return bpb


def build_fsinfo():
    """Build FAT32 FSInfo sector."""
    fsi = bytearray(BLOCK_SIZE)
    # Lead signature
    struct.pack_into('<I', fsi, 0, 0x41615252)
    # Reserved
    # FSInfo signature
    struct.pack_into('<I', fsi, 484, 0x61417272)
    # Last free cluster count (0xFFFFFFFF = unknown)
    struct.pack_into('<I', fsi, 488, 0xFFFFFFFF)
    # Next free cluster hint
    struct.pack_into('<I', fsi, 492, 0xFFFFFFFF)
    # Reserved
    # Trail signature
    struct.pack_into('<I', fsi, 508, 0xAA550000)
    return fsi


def build_fat_entry(value):
    return struct.pack('<I', value & 0x0FFFFFFF)


def make_directory_entries_8dot3(name, ext, attrs, cluster, size):
    """Create a 32-byte 8.3 directory entry."""
    entry = bytearray(32)
    # Name (8 bytes, space-padded)
    name_bytes = name.encode().ljust(8, b' ')[:8]
    ext_bytes = ext.encode().ljust(3, b' ')[:3]
    entry[0:8] = name_bytes
    entry[8:11] = ext_bytes
    entry[11] = attrs
    entry[13] = 0  # creation tenths
    # Cluster high
    struct.pack_into('<H', entry, 20, (cluster >> 16) & 0xFFFF)
    # Cluster low
    struct.pack_into('<H', entry, 26, cluster & 0xFFFF)
    # File size
    struct.pack_into('<I', entry, 28, size)
    return entry


def build_disk_image():
    """Build a complete FAT32 disk image."""
    # Geometry
    sectors_per_cluster = 2  # 1 KB clusters (valid: 2 sectors)
    reserved_sectors = 32
    fat_count = 2
    fat_size = 64          # 64 sectors per FAT = 32K entries
    root_cluster = 2       # always 2 for FAT32
    data_start_lba = PART_LBA + reserved_sectors + fat_count * fat_size
    data_sectors = 64 * sectors_per_cluster  # 64 data clusters
    total_sectors = reserved_sectors + fat_count * fat_size + data_sectors

    total_part_sectors = total_sectors
    image_size = (PART_LBA + total_part_sectors) * BLOCK_SIZE
    img = bytearray(image_size)

    # Write MBR (sector 0)
    mbr = build_mbr(PART_LBA, total_part_sectors)
    img[0:BLOCK_SIZE] = mbr

    # Write BPB at partition start
    bpb = build_bpb(sectors_per_cluster, reserved_sectors, fat_count,
                    fat_size, root_cluster, b'JARVIS_TEST', total_sectors)
    img[PART_LBA * BLOCK_SIZE:(PART_LBA + 1) * BLOCK_SIZE] = bpb

    # Write FSInfo at sector 1
    fsi = build_fsinfo()
    img[(PART_LBA + 1) * BLOCK_SIZE:(PART_LBA + 2) * BLOCK_SIZE] = fsi

    # Write backup BPB at sector 3
    img[(PART_LBA + 3) * BLOCK_SIZE:(PART_LBA + 4) * BLOCK_SIZE] = bpb

    # Write FAT tables
    fat_lba = PART_LBA + reserved_sectors
    for fat_idx in range(fat_count):
        fat_offset = fat_lba * BLOCK_SIZE + fat_idx * fat_size * BLOCK_SIZE
        # Cluster 0: media descriptor (0x0FFFFFF8 | media)
        struct.pack_into('<I', img, fat_offset, 0x0FFFFFF8)
        # Cluster 1: EOC (0x0FFFFFFF)
        struct.pack_into('<I', img, fat_offset + 4, 0x0FFFFFFF)
        # Cluster 2 (root): EOC
        struct.pack_into('<I', img, fat_offset + 8, 0x0FFFFFFF)
        # Cluster 3: EOC (readme.txt)
        struct.pack_into('<I', img, fat_offset + 12, 0x0FFFFFFF)
        # Cluster 4: EOC (hello.txt)
        struct.pack_into('<I', img, fat_offset + 16, 0x0FFFFFFF)
        # Cluster 5: EOC (subdir directory)
        struct.pack_into('<I', img, fat_offset + 20, 0x0FFFFFFF)
        # Cluster 6: -> 7 (multi-cluster file: chain cluster 7)
        struct.pack_into('<I', img, fat_offset + 24, 7)
        # Cluster 7: EOC
        struct.pack_into('<I', img, fat_offset + 28, 0x0FFFFFFF)
        # Cluster 8: EOC (file in subdir)
        struct.pack_into('<I', img, fat_offset + 32, 0x0FFFFFFF)

    # Write root directory entries (cluster 2)
    root_lba = data_start_lba + (root_cluster - 2) * sectors_per_cluster
    root_off = root_lba * BLOCK_SIZE

    # "." entry
    root_img = img
    root_img[root_off:root_off + 32] = make_directory_entries_8dot3(
        '.', '', 0x10, root_cluster, 0)
    # ".." entry (points to itself for root)
    root_img[root_off + 32:root_off + 64] = make_directory_entries_8dot3(
        '..', '', 0x10, root_cluster, 0)

    # README.TXT (cluster 3)
    readme_content = b'This is the README file.\n'
    root_img[root_off + 64:root_off + 96] = make_directory_entries_8dot3(
        'README', 'TXT', 0x20, 3, len(readme_content))

    # HELLO.TXT (cluster 4)
    hello_content = b'Hello, World!\n'
    root_img[root_off + 96:root_off + 128] = make_directory_entries_8dot3(
        'HELLO', 'TXT', 0x20, 4, len(hello_content))

    # SUBDIR (cluster 5)
    root_img[root_off + 128:root_off + 160] = make_directory_entries_8dot3(
        'SUBDIR', '', 0x10, 5, 0)

    # MULTI.TXT (cluster 6, spans clusters 6-7)
    multi_content = b'This file spans multiple clusters. ' * 10
    root_img[root_off + 160:root_off + 192] = make_directory_entries_8dot3(
        'MULTI', 'TXT', 0x20, 6, len(multi_content))

    # Write file contents
    # README.TXT at cluster 3
    cluster3_lba = data_start_lba + (3 - 2) * sectors_per_cluster
    img[cluster3_lba * BLOCK_SIZE:cluster3_lba * BLOCK_SIZE + len(readme_content)] = readme_content

    # HELLO.TXT at cluster 4
    cluster4_lba = data_start_lba + (4 - 2) * sectors_per_cluster
    img[cluster4_lba * BLOCK_SIZE:cluster4_lba * BLOCK_SIZE + len(hello_content)] = hello_content

    # SUBDIR at cluster 5
    cluster5_lba = data_start_lba + (5 - 2) * sectors_per_cluster
    sub_off = cluster5_lba * BLOCK_SIZE
    # "." entry in subdir
    img[sub_off:sub_off + 32] = make_directory_entries_8dot3(
        '.', '', 0x10, 5, 0)
    # ".." entry pointing to root
    img[sub_off + 32:sub_off + 64] = make_directory_entries_8dot3(
        '..', '', 0x10, root_cluster, 0)
    # FILE.TXT in subdir (cluster 8)
    subfile_content = b'I am in a subdirectory.\n'
    img[sub_off + 64:sub_off + 96] = make_directory_entries_8dot3(
        'FILE', 'TXT', 0x20, 8, len(subfile_content))

    # MULTI.TXT at cluster 6
    cluster6_lba = data_start_lba + (6 - 2) * sectors_per_cluster
    for i in range(min(len(multi_content), sectors_per_cluster * BLOCK_SIZE)):
        img[cluster6_lba * BLOCK_SIZE + i] = multi_content[i]

    # Cluster 7 continuation of MULTI.TXT
    cluster7_lba = data_start_lba + (7 - 2) * sectors_per_cluster
    remaining = len(multi_content) - sectors_per_cluster * BLOCK_SIZE
    if remaining > 0:
        for i in range(remaining):
            img[cluster7_lba * BLOCK_SIZE + i] = multi_content[sectors_per_cluster * BLOCK_SIZE + i]

    # FILE.TXT in subdir at cluster 8
    cluster8_lba = data_start_lba + (8 - 2) * sectors_per_cluster
    img[cluster8_lba * BLOCK_SIZE:cluster8_lba * BLOCK_SIZE + len(subfile_content)] = subfile_content

    return img


def main():
    img = build_disk_image()
    with open(sys.argv[1], 'wb') as f:
        f.write(img)
    print(f"Wrote {len(img)} bytes to {sys.argv[1]}")


if __name__ == '__main__':
    main()
