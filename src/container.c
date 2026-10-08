#include "tcomp/container.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tcomp/bitio.h"
#include "tcomp/huffman.h"

/* Layout (docs/FORMAT.md, version 0):
 *   header   magic "TCMP" (4) | format version (1) | method id (1)
 *   STORE    raw bytes to end of file
 *   HUFFMAN  original size, uint64 little-endian (8)
 *            bitstream: 256 x 4-bit code lengths, then one code per byte,
 *            then 0-7 zero bits of padding
 */
enum {
    HEADER_SIZE = 6,
    FORMAT_VERSION = 0,
    COPY_CHUNK = 64 * 1024,
    SIZE_FIELD = 8,
    BYTE_SYMBOLS = 256,
    LENGTH_BITS = 4
};

static const uint8_t MAGIC[4] = {'T', 'C', 'M', 'P'};

/* ---- small I/O helpers -------------------------------------------------- */

/* Copy in -> out until EOF. Buffers are allocated per call, never static,
 * so the library stays safe to call from several threads (M9). */
static tcomp_status copy_stream(FILE *in, FILE *out) {
    uint8_t *buf = malloc(COPY_CHUNK);
    if (buf == NULL) {
        return TCOMP_ERR_NOMEM;
    }
    tcomp_status st = TCOMP_OK;
    size_t n;
    while (st == TCOMP_OK && (n = fread(buf, 1, COPY_CHUNK, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            st = TCOMP_ERR_IO;
        }
    }
    if (st == TCOMP_OK && ferror(in)) {
        st = TCOMP_ERR_IO;
    }
    free(buf);
    return st;
}

static tcomp_status write_all(FILE *out, const void *data, size_t len) {
    return (len == 0 || fwrite(data, 1, len, out) == len) ? TCOMP_OK : TCOMP_ERR_IO;
}

static tcomp_status write_header(FILE *out, tcomp_method method) {
    uint8_t header[HEADER_SIZE];
    memcpy(header, MAGIC, sizeof MAGIC);
    header[4] = FORMAT_VERSION;
    header[5] = (uint8_t)method;
    return write_all(out, header, sizeof header);
}

/* Read everything left in `in` into a new buffer. On success the caller
 * frees *data (which may be NULL only if *len is 0). */
static tcomp_status read_all(FILE *in, uint8_t **data, size_t *len) {
    size_t cap = COPY_CHUNK, n = 0;
    uint8_t *buf = malloc(cap);
    if (buf == NULL) {
        return TCOMP_ERR_NOMEM;
    }
    for (;;) {
        if (n == cap) {
            if (cap > SIZE_MAX / 2) {
                free(buf);
                return TCOMP_ERR_NOMEM;
            }
            uint8_t *grown = realloc(buf, cap * 2);
            if (grown == NULL) {
                free(buf);
                return TCOMP_ERR_NOMEM;
            }
            buf = grown;
            cap *= 2;
        }
        size_t want = cap - n;
        size_t got = fread(buf + n, 1, want, in);
        n += got;
        if (got < want) { /* short read: EOF or error */
            break;
        }
    }
    if (ferror(in)) {
        free(buf);
        return TCOMP_ERR_IO;
    }
    *data = buf;
    *len = n;
    return TCOMP_OK;
}

static void store_le64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint64_t load_le64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

/* ---- compression -------------------------------------------------------- */

static tcomp_status compress_memory(const uint8_t *data, size_t len, tcomp_method method,
                                    FILE *out) {
    uint64_t freqs[BYTE_SYMBOLS] = {0};
    for (size_t i = 0; i < len; i++) {
        freqs[data[i]]++;
    }
    uint8_t lengths[BYTE_SYMBOLS];
    uint16_t codes[BYTE_SYMBOLS];
    tcomp_status st = tcomp_huff_build_lengths(freqs, BYTE_SYMBOLS, TCOMP_HUFF_MAX_BITS, lengths);
    if (st == TCOMP_OK) {
        st = tcomp_huff_assign_codes(lengths, BYTE_SYMBOLS, codes);
    }
    if (st != TCOMP_OK) {
        return st;
    }

    /* The exact HUFFMAN payload size is known before encoding anything, so
     * AUTO can choose without doing the work twice. */
    uint64_t bits = (uint64_t)BYTE_SYMBOLS * LENGTH_BITS;
    for (size_t s = 0; s < BYTE_SYMBOLS; s++) {
        bits += freqs[s] * lengths[s];
    }
    uint64_t huffman_payload = SIZE_FIELD + (bits + 7) / 8;
    if (method == TCOMP_METHOD_AUTO) {
        method = huffman_payload < len ? TCOMP_METHOD_HUFFMAN : TCOMP_METHOD_STORE;
    }

    if (method == TCOMP_METHOD_STORE) {
        st = write_header(out, TCOMP_METHOD_STORE);
        return st != TCOMP_OK ? st : write_all(out, data, len);
    }

    tcomp_bitwriter bw;
    st = tcomp_bw_init(&bw, (size_t)((bits + 7) / 8));
    for (size_t s = 0; s < BYTE_SYMBOLS && st == TCOMP_OK; s++) {
        st = tcomp_bw_write_bits(&bw, lengths[s], LENGTH_BITS);
    }
    for (size_t i = 0; i < len && st == TCOMP_OK; i++) {
        st = tcomp_bw_write_bits(&bw, codes[data[i]], lengths[data[i]]);
    }
    if (st == TCOMP_OK) {
        st = tcomp_bw_flush(&bw);
    }

    uint8_t size_field[SIZE_FIELD];
    store_le64(size_field, len);
    if (st == TCOMP_OK) {
        st = write_header(out, TCOMP_METHOD_HUFFMAN);
    }
    if (st == TCOMP_OK) {
        st = write_all(out, size_field, sizeof size_field);
    }
    if (st == TCOMP_OK) {
        st = write_all(out, tcomp_bw_data(&bw), tcomp_bw_size(&bw));
    }
    tcomp_bw_free(&bw);
    return st;
}

tcomp_status tcomp_compress_stream(FILE *in, FILE *out, tcomp_method method) {
    if (in == NULL || out == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    if (method == TCOMP_METHOD_STORE) {
        tcomp_status st = write_header(out, TCOMP_METHOD_STORE);
        return st != TCOMP_OK ? st : copy_stream(in, out);
    }
    if (method != TCOMP_METHOD_HUFFMAN && method != TCOMP_METHOD_AUTO) {
        return TCOMP_ERR_INVALID_ARG;
    }

    uint8_t *data = NULL;
    size_t len = 0;
    tcomp_status st = read_all(in, &data, &len);
    if (st == TCOMP_OK) {
        st = compress_memory(data, len, method, out);
    }
    free(data);
    return st;
}

/* ---- decompression ------------------------------------------------------ */

static tcomp_status decode_huffman_payload(const uint8_t *payload, size_t len, uint64_t size,
                                           FILE *out) {
    tcomp_bitreader br;
    tcomp_status st = tcomp_br_init(&br, payload, len);
    if (st != TCOMP_OK) {
        return st;
    }

    uint8_t lengths[BYTE_SYMBOLS];
    for (size_t s = 0; s < BYTE_SYMBOLS; s++) {
        uint32_t v;
        st = tcomp_br_read_bits(&br, LENGTH_BITS, &v);
        if (st != TCOMP_OK) {
            return st;
        }
        lengths[s] = (uint8_t)v;
    }

    /* Every symbol costs at least one bit, so a claimed size larger than the
     * bits left cannot be genuine. Rejecting it here means a hostile size
     * field can never make us run long or write gigabytes of output. */
    if (size > tcomp_br_bits_remaining(&br)) {
        return TCOMP_ERR_TRUNCATED;
    }

    tcomp_huff_decoder dec;
    st = tcomp_huff_decoder_init(&dec, lengths, BYTE_SYMBOLS);
    uint8_t *buf = st == TCOMP_OK ? malloc(COPY_CHUNK) : NULL;
    if (st == TCOMP_OK && buf == NULL) {
        st = TCOMP_ERR_NOMEM;
    }
    if (st != TCOMP_OK) {
        tcomp_huff_decoder_free(&dec);
        return st;
    }

    size_t used = 0;
    for (uint64_t i = 0; i < size && st == TCOMP_OK; i++) {
        unsigned sym;
        st = tcomp_huff_decode_symbol(&dec, &br, &sym);
        if (st == TCOMP_OK) {
            buf[used++] = (uint8_t)sym;
            if (used == COPY_CHUNK) {
                st = write_all(out, buf, used);
                used = 0;
            }
        }
    }
    tcomp_huff_decoder_free(&dec);

    /* After the last symbol only zero padding (under one byte) may remain.
     * Anything else means the size field and the data disagree. */
    if (st == TCOMP_OK) {
        size_t left = tcomp_br_bits_remaining(&br);
        if (left >= 8 || tcomp_br_peek_bits(&br, (unsigned)left) != 0) {
            st = TCOMP_ERR_CORRUPT;
        }
    }
    if (st == TCOMP_OK) {
        st = write_all(out, buf, used);
    }
    free(buf);
    return st;
}

static tcomp_status decompress_huffman(FILE *in, FILE *out) {
    uint8_t size_field[SIZE_FIELD];
    size_t got = fread(size_field, 1, sizeof size_field, in);
    if (ferror(in)) {
        return TCOMP_ERR_IO;
    }
    if (got < sizeof size_field) {
        return TCOMP_ERR_TRUNCATED;
    }

    uint8_t *payload = NULL;
    size_t len = 0;
    tcomp_status st = read_all(in, &payload, &len);
    if (st == TCOMP_OK) {
        st = decode_huffman_payload(payload, len, load_le64(size_field), out);
    }
    free(payload);
    return st;
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
    switch (header[5]) {
    case TCOMP_METHOD_STORE:
        return copy_stream(in, out);
    case TCOMP_METHOD_HUFFMAN:
        return decompress_huffman(in, out);
    default:
        return TCOMP_ERR_BAD_METHOD;
    }
}
