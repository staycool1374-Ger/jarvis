#pragma once

#include <types.hpp>

#define FDT_MAGIC       0xD00DFEED
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

#define FDT_V16         16
#define FDT_V17         17

#define FDT_FIRST_SUPPORTED_VERSION  FDT_V16
#define FDT_LAST_SUPPORTED_VERSION   FDT_V17

#define FDT_TAGSIZE     4
#define FDT_V17_SIZE    40

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

struct fdt_reserve_entry {
    uint64_t address;
    uint64_t size;
};

struct fdt_node_header {
    uint32_t tag;
    char name[];
};

struct fdt_property {
    uint32_t tag;
    uint32_t len;
    uint32_t nameoff;
    char data[];
};
