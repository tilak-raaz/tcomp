#include "tcomp/status.h"

const char *tcomp_strerror(tcomp_status status) {
    switch (status) {
    case TCOMP_OK:
        return "success";
    case TCOMP_ERR_IO:
        return "I/O error";
    case TCOMP_ERR_NOMEM:
        return "out of memory";
    case TCOMP_ERR_INVALID_ARG:
        return "invalid argument";
    case TCOMP_ERR_BAD_MAGIC:
        return "not a tcomp file (bad magic bytes)";
    case TCOMP_ERR_BAD_VERSION:
        return "unsupported format version";
    case TCOMP_ERR_BAD_METHOD:
        return "unknown compression method";
    case TCOMP_ERR_TRUNCATED:
        return "file is truncated";
    case TCOMP_ERR_CORRUPT:
        return "file is corrupt";
    case TCOMP_STATUS_COUNT:
        break;
    }
    return "unknown error";
}
