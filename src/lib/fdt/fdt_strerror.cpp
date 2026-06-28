#include <fdt/libfdt.h>

extern "C" {

const char *fdt_strerror(int errval) {
    if (errval > 0)
        return "<valid offset/length>";
    if (errval == 0)
        return "<no error>";

    switch (errval) {
    case -FDT_ERR_NOTFOUND:     return "FDT_ERR_NOTFOUND";
    case -FDT_ERR_EXISTS:       return "FDT_ERR_EXISTS";
    case -FDT_ERR_NOSPACE:      return "FDT_ERR_NOSPACE";
    case -FDT_ERR_BADOFFSET:    return "FDT_ERR_BADOFFSET";
    case -FDT_ERR_BADPATH:      return "FDT_ERR_BADPATH";
    case -FDT_ERR_BADPHANDLE:   return "FDT_ERR_BADPHANDLE";
    case -FDT_ERR_BADSTATE:     return "FDT_ERR_BADSTATE";
    case -FDT_ERR_TRUNCATED:    return "FDT_ERR_TRUNCATED";
    case -FDT_ERR_BADMAGIC:     return "FDT_ERR_BADMAGIC";
    case -FDT_ERR_BADVERSION:   return "FDT_ERR_BADVERSION";
    case -FDT_ERR_BADSTRUCTURE: return "FDT_ERR_BADSTRUCTURE";
    case -FDT_ERR_BADLAYOUT:    return "FDT_ERR_BADLAYOUT";
    case -FDT_ERR_INTERNAL:     return "FDT_ERR_INTERNAL";
    case -FDT_ERR_BADNCELLS:    return "FDT_ERR_BADNCELLS";
    case -FDT_ERR_BADVALUE:     return "FDT_ERR_BADVALUE";
    case -FDT_ERR_BADOVERLAY:   return "FDT_ERR_BADOVERLAY";
    case -FDT_ERR_NOPHANDLES:   return "FDT_ERR_NOPHANDLES";
    default:                    return "<unknown error>";
    }
}

} /* extern "C" */
