#pragma once

#include <types.hpp>
#include <fdt/libfdt.h>

/* Byte swap helpers — FDT is big-endian, CPU is little-endian */
static inline uint32_t fdt32_to_cpu(uint32_t x) {
    return __builtin_bswap32(x);
}

static inline uint64_t fdt64_to_cpu(uint64_t x) {
    return __builtin_bswap64(x);
}

static inline void cpu_to_fdt32(uint32_t *p, uint32_t v) {
    *p = __builtin_bswap32(v);
}

/* Internal helpers */
static inline const void *fdt_offset_ptr_(const void *fdt, int offset) {
    const struct fdt_header *h = (const struct fdt_header *)fdt;
    return (const char *)fdt + fdt32_to_cpu(h->off_dt_struct) + offset;
}

static inline uint32_t fdt_next_tag_(const void *fdt, int startoffset,
                                     int *nextoffset) {
    const uint32_t *tagp = (const uint32_t *)fdt_offset_ptr_(fdt, startoffset);
    uint32_t tag = fdt32_to_cpu(*tagp);
    int offset = startoffset + FDT_TAGSIZE;

    *nextoffset = -FDT_ERR_TRUNCATED;

    switch (tag) {
    case FDT_BEGIN_NODE: {
        const char *name = (const char *)fdt_offset_ptr_(fdt, offset);
        if (!name) return FDT_END;
        int namelen = 0;
        while (name[namelen]) ++namelen;
        offset += (namelen + 1 + 3) & ~3;
        break;
    }
    case FDT_PROP: {
        const uint32_t *lenp = (const uint32_t *)fdt_offset_ptr_(fdt, offset);
        if (!lenp) return FDT_END;
        offset += FDT_TAGSIZE + FDT_TAGSIZE + fdt32_to_cpu(*lenp);
        offset = (offset + 3) & ~3;
        break;
    }
    case FDT_END_NODE:
    case FDT_NOP:
        break;
    case FDT_END:
        return tag;
    default:
        return FDT_END;
    }

    *nextoffset = offset;
    return tag;
}

/* Validate the offset points to a valid location within the structure block.
 * Returns 0 on success, negative on error. */
static inline int fdt_check_node_offset_(const void *fdt, int offset) {
    const struct fdt_header *h = (const struct fdt_header *)fdt;
    if ((offset < 0) || (offset % FDT_TAGSIZE)
        || (offset >= (int)fdt32_to_cpu(h->size_dt_struct)))
        return -FDT_ERR_BADOFFSET;
    return 0;
}
