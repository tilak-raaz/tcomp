#include "tcomp/container.h"

#include <stdint.h>
#include <string.h>

/* Header layout (docs/FORMAT.md, version 0):
 *   offset 0  4 bytes  magic "TCMP"
 *   offset 4  1 byte   format version
 *   offset 5  1 byte   method id
 */
enum { HEADER_SIZE = 6, FORMAT_VERSION = 0, COPY_CHUNK = 64 * 1024 };

static const uint8_t MAGIC[4] = {'T', 'C', 'M', 'P'};

/* Copy in -> out until EOF. Returns TCOMP_ERR_IO on any stream error. */
static tcomp_status copy_stream(FILE *in, FILE *out) {
    static uint8_t buf[COPY_CHUNK];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            return TCOMP_ERR_IO;
        }
    }
    return ferror(in) ? TCOMP_ERR_IO : TCOMP_OK;
}

tcomp_status tcomp_compress_stream(FILE *in, FILE *out) {
    if (in == NULL || out == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    uint8_t header[HEADER_SIZE];
    memcpy(header, MAGIC, sizeof MAGIC);
    header[4] = FORMAT_VERSION;
    header[5] = TCOMP_METHOD_STORE;
    if (fwrite(header, 1, sizeof header, out) != sizeof header) {
        return TCOMP_ERR_IO;
    }
    return copy_stream(in, out);
}

tcomp_status tcomp_decompress_stream(FILE *in, FILE *out) {
    if (in == NULL || out == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    uint8_t header[HEADER_SIZE];
    size_t got = fread(header, 1, sizeof header, in);
    if (ferror(in)) {
        return TCOMP_ERR_IO;
    }

    /* Check the magic on whatever we did read first: a 2-byte file that
     * starts "XY" is "not a tcomp file", not "truncated". */
    size_t magic_got = got < sizeof MAGIC ? got : sizeof MAGIC;
    if (memcmp(header, MAGIC, magic_got) != 0) {
        return TCOMP_ERR_BAD_MAGIC;
    }
    if (got < sizeof header) {
        return TCOMP_ERR_TRUNCATED;
    }
    if (header[4] > FORMAT_VERSION) {
        return TCOMP_ERR_BAD_VERSION;
    }
    if (header[5] != TCOMP_METHOD_STORE) {
        return TCOMP_ERR_BAD_METHOD;
    }
    return copy_stream(in, out);
}
