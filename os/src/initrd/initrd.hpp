#pragma once

#include <types.hpp>

namespace initrd {

struct InitrdFile {
    const uint8_t* data;
    uint64_t size;
};

struct InitrdEntry {
    char name[256];
    uint64_t size;
    bool is_dir;
};

void init(const uint8_t* start, const uint8_t* end);
InitrdFile find(const char* path);

bool readdir(uint64_t* pos, InitrdEntry* entry);

} // namespace initrd
