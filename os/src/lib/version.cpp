#include <version.hpp>

namespace kernel {

static const char version_buf_[] =
    "0.0.6-dev";

static const char* const build_date_ = __DATE__;
static const char* const build_time_ = __TIME__;

static char full_buf_[64];

const char* Version::string() {
    return version_buf_;
}

const char* Version::build_date() {
    return build_date_;
}

const char* Version::build_time() {
    return build_time_;
}

const char* Version::full_string() {
    int pos = 0;
    auto append = [&](const char* s) {
        while (*s && pos < 63) full_buf_[pos++] = *s++;
    };

    append("Jarvis RTOS ");
    append(version_buf_);
    append(" (");
    append(build_date_);
    append(" ");
    append(build_time_);
    append(")");
    full_buf_[pos] = '\0';
    return full_buf_;
}

} // namespace kernel
