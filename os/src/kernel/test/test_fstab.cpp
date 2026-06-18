#include <test.hpp>
#include <logger.hpp>
#include <kernel/vfs/vfs.hpp>
#include <string.hpp>

using namespace kernel;

// Replicate the init fstab parsing logic (from kernel.cpp) for unit testing
static int parse_fstab_line(const char* p, const char* end,
                            char* fs_name_out, size_t fs_max,
                            char* mp_out, size_t mp_max) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p >= end || *p == '#' || *p == '\n') return -1;

    size_t n = 0;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && n < fs_max - 1)
        fs_name_out[n++] = *p++;
    fs_name_out[n] = '\0';

    while (p < end && (*p == ' ' || *p == '\t')) ++p;

    n = 0;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && n < mp_max - 1)
        mp_out[n++] = *p++;
    mp_out[n] = '\0';

    if (!fs_name_out[0] || !mp_out[0]) return -1;
    return 0;
}

JARVIS_TEST(fstab_parses_valid_line) {
    const char input[] = "tmpfs /tmp\n";
    char fs[32], mp[64];
    int ret = parse_fstab_line(input, input + sizeof(input) - 1, fs, sizeof(fs), mp, sizeof(mp));
    JARVIS_ASSERT_EQ(0, ret);
    JARVIS_ASSERT_EQ(0, strcmp(fs, "tmpfs"));
    JARVIS_ASSERT_EQ(0, strcmp(mp, "/tmp"));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(fstab_parses_with_extra_spaces) {
    const char input[] = "  tmpfs   /mnt/extra   \n";
    char fs[32], mp[64];
    int ret = parse_fstab_line(input, input + sizeof(input) - 1, fs, sizeof(fs), mp, sizeof(mp));
    JARVIS_ASSERT_EQ(0, ret);
    JARVIS_ASSERT_EQ(0, strcmp(fs, "tmpfs"));
    JARVIS_ASSERT_EQ(0, strcmp(mp, "/mnt/extra"));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(fstab_skips_comment_line) {
    const char input[] = "# this is a comment\n";
    char fs[32], mp[64];
    int ret = parse_fstab_line(input, input + sizeof(input) - 1, fs, sizeof(fs), mp, sizeof(mp));
    JARVIS_ASSERT_EQ(-1, ret);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(fstab_skips_empty_line) {
    const char input[] = "\n";
    char fs[32], mp[64];
    int ret = parse_fstab_line(input, input + sizeof(input) - 1, fs, sizeof(fs), mp, sizeof(mp));
    JARVIS_ASSERT_EQ(-1, ret);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(fstab_rejects_missing_mountpoint) {
    const char input[] = "tmpfs\n";
    char fs[32], mp[64];
    int ret = parse_fstab_line(input, input + sizeof(input) - 1, fs, sizeof(fs), mp, sizeof(mp));
    JARVIS_ASSERT_EQ(-1, ret);
    JARVIS_TEST_PASS();
}

void register_fstab_tests() {
    kernel::Logger::info("Registering fstab tests");
    JARVIS_REGISTER_TEST(fstab_parses_valid_line);
    JARVIS_REGISTER_TEST(fstab_parses_with_extra_spaces);
    JARVIS_REGISTER_TEST(fstab_skips_comment_line);
    JARVIS_REGISTER_TEST(fstab_skips_empty_line);
    JARVIS_REGISTER_TEST(fstab_rejects_missing_mountpoint);
}
