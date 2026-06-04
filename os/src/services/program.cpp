#include <services/program.hpp>

namespace service {

const ProgramRegistry::Program* ProgramRegistry::programs_[MAX_PROGRAMS] = {};
size_t ProgramRegistry::count_ = 0;

void ProgramRegistry::init() {
    count_ = 0;
}

void ProgramRegistry::register_program(const char* name, const char* desc, void (*entry)()) {
    if (count_ >= MAX_PROGRAMS) return;
    auto* prog = new Program{name, desc, entry};
    programs_[count_++] = prog;
}

const ProgramRegistry::Program* ProgramRegistry::find(const char* name) {
    for (size_t i = 0; i < count_; ++i) {
        if (programs_[i]->name) {
            const char* a = programs_[i]->name;
            const char* b = name;
            while (*a && *b && *a == *b) { ++a; ++b; }
            if (*a == '\0' && *b == '\0') return programs_[i];
        }
    }
    return nullptr;
}

} // namespace service
