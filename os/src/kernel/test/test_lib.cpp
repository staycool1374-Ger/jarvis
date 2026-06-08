#include <test.hpp>
#include <string.hpp>
#include <utils.hpp>
#include <error.hpp>
#include <version.hpp>

using namespace kernel;

JARVIS_TEST(string_strlen) {
    JARVIS_ASSERT_EQ(0, strlen(""));
    JARVIS_ASSERT_EQ(5, strlen("hello"));
    JARVIS_ASSERT_EQ(13, strlen("Hello, World!"));
    JARVIS_ASSERT_EQ(1, strlen("x"));
    JARVIS_ASSERT_HEX_EQ(0, strlen("") ? 1 : 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_strcmp) {
    JARVIS_ASSERT_EQ(0, strcmp("", ""));
    JARVIS_ASSERT_EQ(0, strcmp("abc", "abc"));
    JARVIS_ASSERT(strcmp("abc", "abd") < 0);
    JARVIS_ASSERT(strcmp("abd", "abc") > 0);
    JARVIS_ASSERT(strcmp("abc", "ab") != 0);
    JARVIS_ASSERT_EQ(0, strcmp("identical", "identical"));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_strncmp) {
    JARVIS_ASSERT_EQ(0, strncmp("abcde", "abcde", 5));
    JARVIS_ASSERT_EQ(0, strncmp("abcde", "abcxx", 3));
    JARVIS_ASSERT(strncmp("abcde", "abdde", 3) < 0);
    JARVIS_ASSERT_EQ(0, strncmp("", "", 0));
    JARVIS_ASSERT_EQ(0, strncmp("abcdef", "abc", 3));
    JARVIS_ASSERT(strncmp("abc", "abcdef", 6) < 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_strncpy) {
    char buf[32];
    memset(buf, 0xAA, sizeof(buf));
    strncpy(buf, "hello", sizeof(buf));
    JARVIS_ASSERT_EQ(0, strcmp(buf, "hello"));
    strncpy(buf, "", sizeof(buf));
    JARVIS_ASSERT_EQ(0, strcmp(buf, ""));
    strncpy(buf, "short", sizeof(buf));
    JARVIS_ASSERT_EQ(0, strcmp(buf, "short"));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_memcpy) {
    const char src[] = "memory test data";
    char dst[64];
    memset(dst, 0, sizeof(dst));
    memcpy(dst, src, sizeof(src));
    JARVIS_ASSERT_EQ(0, strcmp(dst, src));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_memset) {
    char buf[32];
    memset(buf, 0xFF, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); ++i)
        JARVIS_ASSERT(buf[i] == static_cast<char>(0xFF));
    memset(buf, 0, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); ++i)
        JARVIS_ASSERT(buf[i] == 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(string_memcmp) {
    JARVIS_ASSERT_EQ(0, memcmp("abc", "abc", 3));
    JARVIS_ASSERT(memcmp("abc", "abd", 3) < 0);
    JARVIS_ASSERT(memcmp("abd", "abc", 3) > 0);
    JARVIS_ASSERT_EQ(0, memcmp("", "", 0));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(utils_align_up) {
    JARVIS_ASSERT_HEX_EQ(0x1000, align_up(0x1, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x1000, align_up(0x1000, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x2000, align_up(0x1001, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x1000, align_up(0xFFF, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x4, align_up(0x3, 0x4));
    JARVIS_ASSERT_HEX_EQ(0x0, align_up(0x0, 0x4));
    JARVIS_ASSERT_HEX_EQ(0x1000, align_up(1, 0x1000));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(utils_align_down) {
    JARVIS_ASSERT_HEX_EQ(0x0000, align_down(0x1, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x1000, align_down(0x1000, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x1000, align_down(0x1001, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x0000, align_down(0xFFF, 0x1000));
    JARVIS_ASSERT_HEX_EQ(0x0000, align_down(0x0, 0x4));
    JARVIS_ASSERT_HEX_EQ(0x0FFC, align_down(0x0FFF, 0x4));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(utils_type_traits) {
    JARVIS_ASSERT((is_same<int, int>::value));
    JARVIS_ASSERT((!is_same<int, unsigned int>::value));
    JARVIS_ASSERT((is_same<uint64_t, unsigned long long>::value));
    JARVIS_ASSERT((is_integral<int>::value));
    JARVIS_ASSERT((is_integral<uint64_t>::value));
    JARVIS_ASSERT((!is_integral<double>::value));
    JARVIS_ASSERT((is_pod<int>::value));
    JARVIS_TEST_PASS();
}

JARVIS_TEST(error_or_basic) {
    ErrorOr<int> e0(42);
    JARVIS_ASSERT(e0.ok());
    JARVIS_ASSERT_EQ(42, *e0);
    ErrorOr<int> e1(Error::OOM);
    JARVIS_ASSERT(!e1.ok());
    ErrorOr<void> v0;
    JARVIS_ASSERT(v0.ok());
    ErrorOr<void> v1(Error::INVALID_ARG);
    JARVIS_ASSERT(!v1.ok());
    JARVIS_TEST_PASS();
}

JARVIS_TEST(error_or_errors) {
    ErrorOr<int> e_oom(Error::OOM);
    JARVIS_ASSERT(!e_oom.ok());
    JARVIS_ASSERT(e_oom.error == Error::OOM);
    ErrorOr<int> e_inv(Error::INVALID_ARG);
    JARVIS_ASSERT(e_inv.error == Error::INVALID_ARG);
    ErrorOr<int> e_nf(Error::NOT_FOUND);
    JARVIS_ASSERT(e_nf.error == Error::NOT_FOUND);
    JARVIS_ASSERT(e_nf.ok() == false);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(version_string_not_empty) {
    const char* sv = Version::string();
    JARVIS_ASSERT(sv != nullptr);
    JARVIS_ASSERT(strlen(sv) > 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(version_full_string_not_empty) {
    const char* fv = Version::full_string();
    JARVIS_ASSERT(fv != nullptr);
    JARVIS_ASSERT(strlen(fv) > 0);
    JARVIS_TEST_PASS();
}

JARVIS_TEST(version_build_date_not_empty) {
    const char* bd = Version::build_date();
    JARVIS_ASSERT(bd != nullptr);
    JARVIS_ASSERT(strlen(bd) > 0);
    JARVIS_TEST_PASS();
}

void register_lib_tests() {
    Logger::info("Registering lib tests");

    JARVIS_REGISTER_TEST(string_strlen);
    JARVIS_REGISTER_TEST(string_strcmp);
    JARVIS_REGISTER_TEST(string_strncmp);
    JARVIS_REGISTER_TEST(string_strncpy);
    JARVIS_REGISTER_TEST(string_memcpy);
    JARVIS_REGISTER_TEST(string_memset);
    JARVIS_REGISTER_TEST(string_memcmp);

    JARVIS_REGISTER_TEST(utils_align_up);
    JARVIS_REGISTER_TEST(utils_align_down);
    JARVIS_REGISTER_TEST(utils_type_traits);

    JARVIS_REGISTER_TEST(error_or_basic);
    JARVIS_REGISTER_TEST(error_or_errors);

    JARVIS_REGISTER_TEST(version_string_not_empty);
    JARVIS_REGISTER_TEST(version_full_string_not_empty);
    JARVIS_REGISTER_TEST(version_build_date_not_empty);
}
