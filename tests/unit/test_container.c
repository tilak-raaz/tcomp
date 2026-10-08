/* Unit tests for the .tcmp container (src/container.c) and status messages.
 *
 * Tests use tmpfile() so they never touch the working directory.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tcomp/container.h"
#include "tcomp/status.h"
#include "test.h"

/* ---- helpers ------------------------------------------------------------ */

/* A tmpfile holding exactly the given bytes, rewound to the start. */
static FILE *file_with(const void *data, size_t len) {
    FILE *f = tmpfile();
    if (f == NULL) {
        return NULL;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    return f;
}

/* Read a whole stream into a malloc'd buffer; caller frees. */
static uint8_t *slurp(FILE *f, size_t *len_out) {
    rewind(f);
    size_t cap = 4096, len = 0;
    uint8_t *buf = malloc(cap);
    size_t n;
    while (buf != NULL && (n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            uint8_t *bigger = realloc(buf, cap);
            if (bigger == NULL) {
                free(buf);
                return NULL;
            }
            buf = bigger;
        }
    }
    *len_out = len;
    return buf;
}

/* Compress `data` with `method`; returns the .tcmp bytes (caller frees) or
 * NULL on failure. */
static uint8_t *compress_bytes(const uint8_t *data, size_t len, tcomp_method method,
                               size_t *out_len) {
    FILE *in = file_with(data, len);
    FILE *packed = tmpfile();
    uint8_t *out = NULL;
    if (in && packed && tcomp_compress_stream(in, packed, method) == TCOMP_OK) {
        out = slurp(packed, out_len);
    }
    if (in) fclose(in);
    if (packed) fclose(packed);
    return out;
}

/* compress with `method`, then decompress; 1 if the output matches exactly. */
static int roundtrips_with(const uint8_t *data, size_t len, tcomp_method method) {
    FILE *in = file_with(data, len);
    FILE *packed = tmpfile();
    FILE *restored = tmpfile();
    int ok = in != NULL && packed != NULL && restored != NULL;

    ok = ok && tcomp_compress_stream(in, packed, method) == TCOMP_OK;
    if (ok) {
        rewind(packed);
    }
    ok = ok && tcomp_decompress_stream(packed, restored) == TCOMP_OK;

    size_t out_len = 0;
    uint8_t *out = ok ? slurp(restored, &out_len) : NULL;
    ok = ok && out != NULL && out_len == len && (len == 0 || memcmp(out, data, len) == 0);

    free(out);
    if (in) fclose(in);
    if (packed) fclose(packed);
    if (restored) fclose(restored);
    return ok;
}

/* Roundtrip through every method. */
static int roundtrips(const uint8_t *data, size_t len) {
    return roundtrips_with(data, len, TCOMP_METHOD_STORE) &&
           roundtrips_with(data, len, TCOMP_METHOD_HUFFMAN) &&
           roundtrips_with(data, len, TCOMP_METHOD_AUTO);
}

/* Hand-build a HUFFMAN file: header, size field, 4-bit code lengths
 * (lengths[s] for s = 0..255), then `data`. Returns its length. */
enum { HUFF_HEADER = 6 + 8 + 128 };
static size_t build_huffman_file(uint8_t *buf, uint64_t size, const uint8_t lengths[256],
                                 const uint8_t *data, size_t data_len) {
    const uint8_t header[6] = {'T', 'C', 'M', 'P', 0, TCOMP_METHOD_HUFFMAN};
    memcpy(buf, header, 6);
    for (int i = 0; i < 8; i++) {
        buf[6 + i] = (uint8_t)(size >> (8 * i));
    }
    for (int s = 0; s < 256; s += 2) {
        buf[14 + s / 2] = (uint8_t)(lengths[s] << 4 | lengths[s + 1]);
    }
    if (data_len > 0) { /* memcpy(dst, NULL, 0) is still undefined behaviour */
        memcpy(buf + HUFF_HEADER, data, data_len);
    }
    return HUFF_HEADER + data_len;
}

/* Decompress raw bytes and return the status. */
static tcomp_status decompress_bytes(const void *data, size_t len) {
    FILE *in = file_with(data, len);
    FILE *out = tmpfile();
    tcomp_status st = (in && out) ? tcomp_decompress_stream(in, out) : TCOMP_ERR_IO;
    if (in) fclose(in);
    if (out) fclose(out);
    return st;
}

/* ---- status ------------------------------------------------------------- */

TEST(test_strerror_covers_every_code) {
    for (int s = 0; s < TCOMP_STATUS_COUNT; s++) {
        const char *msg = tcomp_strerror((tcomp_status)s);
        CHECK(msg != NULL);
        CHECK(strcmp(msg, "unknown error") != 0);
    }
}

TEST(test_strerror_unknown_code) {
    CHECK(strcmp(tcomp_strerror((tcomp_status)999), "unknown error") == 0);
}

/* ---- container roundtrips ----------------------------------------------- */

TEST(test_roundtrip_empty) { CHECK(roundtrips(NULL, 0)); }

TEST(test_roundtrip_one_byte) {
    const uint8_t b[] = {0x42};
    CHECK(roundtrips(b, sizeof b));
}

TEST(test_roundtrip_all_byte_values) {
    uint8_t b[256];
    for (int i = 0; i < 256; i++) {
        b[i] = (uint8_t)i;
    }
    CHECK(roundtrips(b, sizeof b));
}

TEST(test_roundtrip_larger_than_copy_chunk) {
    size_t len = 200 * 1024 + 7; /* not a multiple of the internal buffer */
    uint8_t *b = malloc(len);
    CHECK(b != NULL);
    if (b == NULL) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        b[i] = (uint8_t)(i * 31u + 7u);
    }
    CHECK(roundtrips(b, len));
    free(b);
}

TEST(test_roundtrip_skewed_text) {
    /* Text-like data: Huffman's normal case. */
    const char *words[] = {"the ",   "quick ", "brown ", "fox ",
                           "jumps ", "over ",  "a ",     "lazy dog\n"};
    size_t len = 0, cap = 100000;
    uint8_t *b = malloc(cap);
    CHECK(b != NULL);
    if (b == NULL) {
        return;
    }
    for (unsigned i = 0; len + 16 < cap; i = (i * 7 + 3) % 8) {
        size_t w = strlen(words[i]);
        memcpy(b + len, words[i], w);
        len += w;
    }
    CHECK(roundtrips(b, len));
    free(b);
}

/* ---- method selection --------------------------------------------------- */

TEST(test_auto_uses_huffman_when_smaller) {
    uint8_t text[4000];
    for (size_t i = 0; i < sizeof text; i++) {
        text[i] = "aaaaaaabbbccd"[i % 13];
    }
    size_t out_len = 0;
    uint8_t *out = compress_bytes(text, sizeof text, TCOMP_METHOD_AUTO, &out_len);
    CHECK(out != NULL && out[5] == TCOMP_METHOD_HUFFMAN);
    CHECK(out_len < sizeof text / 2);
    free(out);
}

TEST(test_auto_falls_back_to_store) {
    /* Small or incompressible input: 136 bytes of Huffman overhead can't pay off. */
    const uint8_t tiny[] = "hello";
    size_t out_len = 0;
    uint8_t *out = compress_bytes(tiny, 5, TCOMP_METHOD_AUTO, &out_len);
    CHECK(out != NULL && out[5] == TCOMP_METHOD_STORE && out_len == 6 + 5);
    free(out);

    uint8_t every_byte[1024];
    for (size_t i = 0; i < sizeof every_byte; i++) {
        every_byte[i] = (uint8_t)i; /* uniform: Huffman gains nothing */
    }
    out = compress_bytes(every_byte, sizeof every_byte, TCOMP_METHOD_AUTO, &out_len);
    CHECK(out != NULL && out[5] == TCOMP_METHOD_STORE && out_len == 6 + sizeof every_byte);
    free(out);
}

TEST(test_huffman_exact_size) {
    /* 1001 copies of one byte: 1-bit codes. Payload = 8 (size) + ceil((1024
     * length-table bits + 1001 data bits) / 8) = 8 + 254. */
    uint8_t same[1001];
    memset(same, 'A', sizeof same);
    size_t out_len = 0;
    uint8_t *out = compress_bytes(same, sizeof same, TCOMP_METHOD_HUFFMAN, &out_len);
    CHECK(out != NULL && out_len == 6 + 8 + 254);
    free(out);
}

TEST(test_compress_rejects_unknown_method) {
    FILE *in = file_with("x", 1);
    FILE *out = tmpfile();
    CHECK(tcomp_compress_stream(in, out, (tcomp_method)7) == TCOMP_ERR_INVALID_ARG);
    if (in) fclose(in);
    if (out) fclose(out);
}

/* ---- container rejects bad input ---------------------------------------- */

TEST(test_rejects_empty_input) { CHECK(decompress_bytes(NULL, 0) == TCOMP_ERR_TRUNCATED); }

TEST(test_rejects_bad_magic) {
    const char junk[] = "HELLO, WORLD";
    CHECK(decompress_bytes(junk, sizeof junk - 1) == TCOMP_ERR_BAD_MAGIC);
}

TEST(test_rejects_short_bad_magic) {
    /* Too short to be a header, but clearly not ours: report bad magic. */
    CHECK(decompress_bytes("XY", 2) == TCOMP_ERR_BAD_MAGIC);
}

TEST(test_rejects_truncated_header) {
    CHECK(decompress_bytes("TCM", 3) == TCOMP_ERR_TRUNCATED);
    CHECK(decompress_bytes("TCMP\0", 5) == TCOMP_ERR_TRUNCATED);
}

TEST(test_rejects_future_version) {
    const uint8_t h[] = {'T', 'C', 'M', 'P', 99, TCOMP_METHOD_STORE};
    CHECK(decompress_bytes(h, sizeof h) == TCOMP_ERR_BAD_VERSION);
}

TEST(test_rejects_unknown_method) {
    const uint8_t h[] = {'T', 'C', 'M', 'P', 0, 200};
    CHECK(decompress_bytes(h, sizeof h) == TCOMP_ERR_BAD_METHOD);
}

/* ---- HUFFMAN payload rejects bad input ---------------------------------- */

TEST(test_huffman_rejects_truncated_fields) {
    const uint8_t short_size[] = {'T', 'C', 'M', 'P', 0, TCOMP_METHOD_HUFFMAN, 1, 0, 0};
    CHECK(decompress_bytes(short_size, sizeof short_size) == TCOMP_ERR_TRUNCATED);

    uint8_t buf[HUFF_HEADER + 4];
    uint8_t lengths[256] = {0};
    lengths['A'] = 1;
    size_t n = build_huffman_file(buf, 1, lengths, NULL, 0);
    CHECK(decompress_bytes(buf, n - 60) == TCOMP_ERR_TRUNCATED); /* cut inside table */
}

TEST(test_huffman_rejects_impossible_lengths) {
    uint8_t buf[HUFF_HEADER + 4];
    uint8_t lengths[256];
    memset(lengths, 1, sizeof lengths); /* 256 one-bit codes: oversubscribed */
    const uint8_t data[] = {0x00};
    size_t n = build_huffman_file(buf, 1, lengths, data, 1);
    CHECK(decompress_bytes(buf, n) == TCOMP_ERR_CORRUPT);
}

TEST(test_huffman_rejects_unused_code) {
    uint8_t buf[HUFF_HEADER + 4];
    uint8_t lengths[256] = {0};
    lengths['A'] = 1;              /* only code '0' */
    const uint8_t data[] = {0x80}; /* begins with '1' */
    size_t n = build_huffman_file(buf, 1, lengths, data, 1);
    CHECK(decompress_bytes(buf, n) == TCOMP_ERR_CORRUPT);

    memset(lengths, 0, sizeof lengths); /* no codes at all, but size says 1 */
    n = build_huffman_file(buf, 1, lengths, data, 1);
    CHECK(decompress_bytes(buf, n) == TCOMP_ERR_CORRUPT);
}

TEST(test_huffman_rejects_huge_size_quickly) {
    /* A hostile size field must fail before any work or allocation. */
    uint8_t buf[HUFF_HEADER + 4];
    uint8_t lengths[256] = {0};
    lengths['A'] = 1;
    const uint8_t data[] = {0x00, 0x00};
    size_t n = build_huffman_file(buf, UINT64_MAX, lengths, data, 2);
    CHECK(decompress_bytes(buf, n) == TCOMP_ERR_TRUNCATED);
    n = build_huffman_file(buf, 17, lengths, data, 2); /* 17 symbols > 16 bits */
    CHECK(decompress_bytes(buf, n) == TCOMP_ERR_TRUNCATED);
}

TEST(test_huffman_validates_size_before_writing) {
    /* 1 MiB of '0' bits decodes to 8 Mi copies of 'A'. Claiming one more
     * symbol than that must fail up front: no decoding, no output written. */
    enum { DATA_LEN = 1 << 20 };
    uint8_t *buf = calloc(HUFF_HEADER + DATA_LEN, 1);
    CHECK(buf != NULL);
    if (buf == NULL) {
        return;
    }
    uint8_t lengths[256] = {0};
    lengths['A'] = 1;
    size_t n = build_huffman_file(buf, (uint64_t)DATA_LEN * 8 + 1, lengths, NULL, 0);
    n += DATA_LEN; /* data bytes are already zero */

    FILE *in = file_with(buf, n);
    FILE *out = tmpfile();
    CHECK(in != NULL && out != NULL);
    if (in && out) {
        CHECK(tcomp_decompress_stream(in, out) == TCOMP_ERR_TRUNCATED);
        CHECK(ftell(out) == 0);
    }
    if (in) fclose(in);
    if (out) fclose(out);
    free(buf);
}

TEST(test_huffman_rejects_trailing_data) {
    uint8_t same[1001];
    memset(same, 'A', sizeof same);
    size_t len = 0;
    uint8_t *good = compress_bytes(same, sizeof same, TCOMP_METHOD_HUFFMAN, &len);
    CHECK(good != NULL);
    if (good == NULL) {
        return;
    }
    CHECK(decompress_bytes(good, len) == TCOMP_OK);

    /* Last byte holds 1 data bit + 7 padding bits; padding must be zero. */
    good[len - 1] |= 0x01;
    CHECK(decompress_bytes(good, len) == TCOMP_ERR_CORRUPT);
    good[len - 1] &= (uint8_t)~0x01;

    uint8_t *longer = malloc(len + 1); /* an extra whole byte after the data */
    CHECK(longer != NULL);
    if (longer != NULL) {
        memcpy(longer, good, len);
        longer[len] = 0x00;
        CHECK(decompress_bytes(longer, len + 1) == TCOMP_ERR_CORRUPT);
        free(longer);
    }
    free(good);
}

TEST(test_null_arguments) {
    FILE *f = tmpfile();
    CHECK(tcomp_compress_stream(NULL, f, TCOMP_METHOD_AUTO) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_compress_stream(f, NULL, TCOMP_METHOD_AUTO) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_decompress_stream(NULL, f) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_decompress_stream(f, NULL) == TCOMP_ERR_INVALID_ARG);
    if (f) fclose(f);
}

void suite_container(void) {
    RUN(test_strerror_covers_every_code);
    RUN(test_strerror_unknown_code);

    RUN(test_roundtrip_empty);
    RUN(test_roundtrip_one_byte);
    RUN(test_roundtrip_all_byte_values);
    RUN(test_roundtrip_larger_than_copy_chunk);
    RUN(test_roundtrip_skewed_text);

    RUN(test_auto_uses_huffman_when_smaller);
    RUN(test_auto_falls_back_to_store);
    RUN(test_huffman_exact_size);
    RUN(test_compress_rejects_unknown_method);

    RUN(test_rejects_empty_input);
    RUN(test_rejects_bad_magic);
    RUN(test_rejects_short_bad_magic);
    RUN(test_rejects_truncated_header);
    RUN(test_rejects_future_version);
    RUN(test_rejects_unknown_method);

    RUN(test_huffman_rejects_truncated_fields);
    RUN(test_huffman_rejects_impossible_lengths);
    RUN(test_huffman_rejects_unused_code);
    RUN(test_huffman_rejects_huge_size_quickly);
    RUN(test_huffman_validates_size_before_writing);
    RUN(test_huffman_rejects_trailing_data);

    RUN(test_null_arguments);
}
