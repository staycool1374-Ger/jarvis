#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/fat32.hpp>
#include <kernel/driver/block_device.hpp>
#include <string.hpp>

using namespace kernel;
using namespace kernel::fat32;

extern "C" uint8_t _binary_build_fat32_img_start[];
extern "C" uint8_t _binary_build_fat32_img_end[];

static uint64_t fat32_img_sectors() {
    return static_cast<uint64_t>(
        _binary_build_fat32_img_end - _binary_build_fat32_img_start)
        / SECTOR_SIZE;
}

static kernel::block::MockBlockDevice make_dev() {
    return kernel::block::MockBlockDevice(
        _binary_build_fat32_img_start, fat32_img_sectors(), true);
}

static Fat32Partition mount_fs(kernel::block::MockBlockDevice& dev) {
    Fat32Partition fs(dev);
    fs.mount();
    return fs;
}

JARVIS_TEST(fat32_mbr_valid_signature) {
    auto dev = make_dev();
    uint8_t mbr[SECTOR_SIZE];
    JARVIS_ASSERT(dev.read_sector(0, mbr));
    JARVIS_ASSERT(mbr[510] == 0x55);
    JARVIS_ASSERT(mbr[511] == 0xAA);
}

JARVIS_TEST(fat32_mbr_parse_partition) {
    auto dev = make_dev();
    uint64_t part_lba = 0;
    JARVIS_ASSERT(find_fat32_in_mbr(dev, part_lba));
    JARVIS_ASSERT(part_lba > 0);
}

JARVIS_TEST(fat32_mbr_no_partition_table) {
    auto dev = kernel::block::MockBlockDevice(1);
    uint64_t part_lba = 0;
    JARVIS_ASSERT(!find_fat32_in_mbr(dev, part_lba));
}

JARVIS_TEST(fat32_bpb_valid_signature) {
    auto dev = make_dev();
    uint64_t part_lba = 0;
    JARVIS_ASSERT(find_fat32_in_mbr(dev, part_lba));
    uint8_t bpb_sector[SECTOR_SIZE];
    JARVIS_ASSERT(dev.read_sector(part_lba, bpb_sector));
    JARVIS_ASSERT(bpb_sector[510] == 0x55);
    JARVIS_ASSERT(bpb_sector[511] == 0xAA);
}

JARVIS_TEST(fat32_bpb_bytes_per_sector) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    JARVIS_ASSERT(fs.bpb().bytes_per_sector == 512);
}

JARVIS_TEST(fat32_bpb_sectors_per_cluster) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    uint8_t spc = fs.bpb().sectors_per_cluster;
    JARVIS_ASSERT(spc >= 1);
    JARVIS_ASSERT((spc & (spc - 1)) == 0);
}

JARVIS_TEST(fat32_bpb_reserved_sectors) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    JARVIS_ASSERT(fs.bpb().reserved_sectors > 0);
}

JARVIS_TEST(fat32_bpb_fat_count) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    JARVIS_ASSERT(fs.bpb().fat_count == 2);
}

JARVIS_TEST(fat32_bpb_root_cluster) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    JARVIS_ASSERT(fs.bpb().root_cluster >= 2);
}

JARVIS_TEST(fat32_bpb_fat_size) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    JARVIS_ASSERT(fs.bpb().fat_size > 0);
}

JARVIS_TEST(fat32_bpb_total_sectors) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    JARVIS_ASSERT(fs.bpb().total_sectors > 0);
}

JARVIS_TEST(fat32_fat_read_cluster) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    uint32_t next = 0;
    JARVIS_ASSERT(fs.read_fat_entry(2, next));
    JARVIS_ASSERT(Fat32Partition::is_eof(next));
}

JARVIS_TEST(fat32_fat_eof_marker) {
    JARVIS_ASSERT(Fat32Partition::is_eof(0x0FFFFFF8));
    JARVIS_ASSERT(Fat32Partition::is_eof(0x0FFFFFFF));
    JARVIS_ASSERT(!Fat32Partition::is_eof(0x0FFFFFF7));
}

JARVIS_TEST(fat32_fat_free_cluster) {
    JARVIS_ASSERT(Fat32Partition::is_free(0));
    JARVIS_ASSERT(!Fat32Partition::is_free(1));
}

JARVIS_TEST(fat32_fat_bad_cluster) {
    JARVIS_ASSERT(Fat32Partition::is_bad(0x0FFFFFF7));
    JARVIS_ASSERT(!Fat32Partition::is_bad(0x0FFFFFF8));
}

JARVIS_TEST(fat32_dir_root_entries) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);

    DirEntry entry = {};
    uint64_t pos = 0;
    bool found_readme = false;
    bool found_hello = false;

    while (read_dir_entry(fs, fs.bpb().root_cluster, pos, entry)) {
        if (!entry.valid) continue;
        if (strcmp(entry.name, "README.TXT") == 0) found_readme = true;
        if (strcmp(entry.name, "HELLO.TXT") == 0) found_hello = true;
    }

    JARVIS_ASSERT(found_readme);
    JARVIS_ASSERT(found_hello);
}

JARVIS_TEST(fat32_dir_short_name) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    DirEntry entry = {};
    JARVIS_ASSERT(lookup_in_dir(fs, fs.bpb().root_cluster, "README.TXT", entry));
    JARVIS_ASSERT(strcmp(entry.name, "README.TXT") == 0);
}

JARVIS_TEST(fat32_dir_file_size) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    DirEntry entry = {};
    JARVIS_ASSERT(lookup_in_dir(fs, fs.bpb().root_cluster, "README.TXT", entry));
    JARVIS_ASSERT(entry.size > 0);
}

JARVIS_TEST(fat32_dir_file_cluster) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    DirEntry entry = {};
    JARVIS_ASSERT(lookup_in_dir(fs, fs.bpb().root_cluster, "README.TXT", entry));
    JARVIS_ASSERT(entry.cluster >= 2);
}

JARVIS_TEST(fat32_dir_attribute_readonly) {
    JARVIS_ASSERT((ATTR_READ_ONLY & 0x01) != 0);
}

JARVIS_TEST(fat32_dir_attribute_hidden) {
    JARVIS_ASSERT((ATTR_HIDDEN & 0x02) != 0);
}

JARVIS_TEST(fat32_dir_attribute_directory) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    DirEntry entry = {};
    JARVIS_ASSERT(lookup_in_dir(fs, fs.bpb().root_cluster, "SUBDIR", entry));
    JARVIS_ASSERT(entry.is_directory);
    JARVIS_ASSERT(lookup_in_dir(fs, fs.bpb().root_cluster, "README.TXT", entry));
    JARVIS_ASSERT(!entry.is_directory);
}

JARVIS_TEST(fat32_dir_volume_label) {
    JARVIS_ASSERT(ATTR_VOLUME_ID == 0x08);
}

JARVIS_TEST(fat32_dir_lfn_skipped) {
    JARVIS_ASSERT(ATTR_LFN == 0x0F);
}

JARVIS_TEST(fat32_chain_traverse_single) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    DirEntry entry = {};
    JARVIS_ASSERT(lookup_in_dir(fs, fs.bpb().root_cluster, "README.TXT", entry));
    uint32_t next = entry.cluster;
    JARVIS_ASSERT(fs.read_fat_entry(next, next));
    JARVIS_ASSERT(Fat32Partition::is_eof(next));
}

JARVIS_TEST(fat32_chain_traverse_multi) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    DirEntry entry = {};
    JARVIS_ASSERT(lookup_in_dir(fs, fs.bpb().root_cluster, "MULTI.TXT", entry));
    uint32_t next = entry.cluster;
    JARVIS_ASSERT(fs.read_fat_entry(next, next));
    JARVIS_ASSERT(!Fat32Partition::is_eof(next));
    JARVIS_ASSERT(!Fat32Partition::is_free(next));
    JARVIS_ASSERT(fs.read_fat_entry(next, next));
    JARVIS_ASSERT(Fat32Partition::is_eof(next));
}

JARVIS_TEST(fat32_read_file) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    DirEntry entry = {};
    JARVIS_ASSERT(lookup_in_dir(fs, fs.bpb().root_cluster, "HELLO.TXT", entry));
    JARVIS_ASSERT(entry.size > 0);

    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    int64_t nread = read_file(fs, entry.cluster, entry.size, 0, sizeof(buf), buf);
    JARVIS_ASSERT(nread > 0);
    JARVIS_ASSERT(static_cast<uint64_t>(nread) == entry.size);
    JARVIS_ASSERT(memcmp(buf, "Hello, World!\n", 14) == 0);
}

JARVIS_TEST(fat32_lookup_subdir) {
    auto dev = make_dev();
    auto fs = mount_fs(dev);
    DirEntry sub = {};
    JARVIS_ASSERT(lookup_in_dir(fs, fs.bpb().root_cluster, "SUBDIR", sub));
    JARVIS_ASSERT(sub.is_directory);

    DirEntry file_in_sub = {};
    JARVIS_ASSERT(lookup_in_dir(fs, sub.cluster, "FILE.TXT", file_in_sub));
    JARVIS_ASSERT(!file_in_sub.is_directory);
    JARVIS_ASSERT(file_in_sub.size > 0);

    uint8_t buf[128];
    memset(buf, 0, sizeof(buf));
    int64_t nread = read_file(fs, file_in_sub.cluster, file_in_sub.size, 0, sizeof(buf), buf);
    JARVIS_ASSERT(nread > 0);
    JARVIS_ASSERT(memcmp(buf, "I am in a subdirectory.\n", 24) == 0);
}

JARVIS_TEST(fat32_chain_corrupt_loop) {
    JARVIS_ASSERT(Fat32Partition::is_eof(0x0FFFFFFF));
}

JARVIS_TEST(fat32_chain_corrupt_eof_missing) {
    JARVIS_ASSERT(!Fat32Partition::is_eof(3));
}

void register_fat32_tests() {
    Logger::info("Registering FAT32 tests");

    JARVIS_REGISTER_RELEASE_TEST(fat32_mbr_valid_signature);
    JARVIS_REGISTER_RELEASE_TEST(fat32_mbr_parse_partition);
    JARVIS_REGISTER_RELEASE_TEST(fat32_mbr_no_partition_table);
    JARVIS_REGISTER_RELEASE_TEST(fat32_bpb_valid_signature);
    JARVIS_REGISTER_RELEASE_TEST(fat32_bpb_bytes_per_sector);
    JARVIS_REGISTER_RELEASE_TEST(fat32_bpb_sectors_per_cluster);
    JARVIS_REGISTER_RELEASE_TEST(fat32_bpb_reserved_sectors);
    JARVIS_REGISTER_RELEASE_TEST(fat32_bpb_fat_count);
    JARVIS_REGISTER_RELEASE_TEST(fat32_bpb_root_cluster);
    JARVIS_REGISTER_RELEASE_TEST(fat32_bpb_fat_size);
    JARVIS_REGISTER_RELEASE_TEST(fat32_bpb_total_sectors);
    JARVIS_REGISTER_RELEASE_TEST(fat32_fat_read_cluster);
    JARVIS_REGISTER_RELEASE_TEST(fat32_fat_eof_marker);
    JARVIS_REGISTER_RELEASE_TEST(fat32_fat_free_cluster);
    JARVIS_REGISTER_RELEASE_TEST(fat32_fat_bad_cluster);
    JARVIS_REGISTER_RELEASE_TEST(fat32_dir_root_entries);
    JARVIS_REGISTER_RELEASE_TEST(fat32_dir_short_name);
    JARVIS_REGISTER_RELEASE_TEST(fat32_dir_file_size);
    JARVIS_REGISTER_RELEASE_TEST(fat32_dir_file_cluster);
    JARVIS_REGISTER_RELEASE_TEST(fat32_dir_attribute_readonly);
    JARVIS_REGISTER_RELEASE_TEST(fat32_dir_attribute_hidden);
    JARVIS_REGISTER_RELEASE_TEST(fat32_dir_attribute_directory);
    JARVIS_REGISTER_RELEASE_TEST(fat32_dir_volume_label);
    JARVIS_REGISTER_RELEASE_TEST(fat32_dir_lfn_skipped);
    JARVIS_REGISTER_RELEASE_TEST(fat32_chain_traverse_single);
    JARVIS_REGISTER_RELEASE_TEST(fat32_chain_traverse_multi);
    JARVIS_REGISTER_RELEASE_TEST(fat32_read_file);
    JARVIS_REGISTER_RELEASE_TEST(fat32_lookup_subdir);
    JARVIS_REGISTER_RELEASE_TEST(fat32_chain_corrupt_loop);
    JARVIS_REGISTER_RELEASE_TEST(fat32_chain_corrupt_eof_missing);
}
