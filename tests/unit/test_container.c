/* Unit tests for the .tcmp container (src/container.c) and status messages.
 *
 * Tests use tmpfile() and in-memory streams, so they never touch the
 * working directory.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tcomp/bitio.h"
#include "tcomp/container.h"
#include "tcomp/crc32.h"
#include "tcomp/lz77.h"
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
           roundtrips_with(data, len, TCOMP_METHOD_LZ77) &&
           roundtrips_with(data, len, TCOMP_METHOD_LZH) &&
           roundtrips_with(data, len, TCOMP_METHOD_AUTO);
}

/* Write a version 1 header (15 bytes) for `method` and original `size`. */
enum { V1_HEADER = 15, V1_TRAILER = 4 };
static void put_header(uint8_t *buf, tcomp_method method, uint64_t size) {
    const uint8_t start[7] = {'T', 'C', 'M', 'P', 1, (uint8_t)method, 0};
    memcpy(buf, start, sizeof start);
    for (int i = 0; i < 8; i++) {
        buf[7 + i] = (uint8_t)(size >> (8 * i));
    }
}

static void put_trailer(uint8_t *at, uint32_t crc) {
    for (int i = 0; i < 4; i++) {
        at[i] = (uint8_t)(crc >> (8 * i));
    }
}

/* Hand-build an LZ77 file from tokens, encoded exactly as FORMAT.md says.
 * Returns a malloc'd buffer (caller frees) and its length in *out_len.
 * Lets tests create token streams the parser would never produce. */
static uint8_t *build_lz77_file(const tcomp_lz77_token *tokens, size_t count, uint64_t size,
                                uint32_t crc, size_t *out_len) {
    tcomp_bitwriter bw;
    if (tcomp_bw_init(&bw, 0) != TCOMP_OK) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].distance == 0) {
            tcomp_bw_write_bits(&bw, tokens[i].length, 9);
        } else {
            tcomp_bw_write_bits(&bw, 1, 1);
            tcomp_bw_write_bits(&bw, tokens[i].length - 3u, 8);
            tcomp_bw_write_bits(&bw, tokens[i].distance - 1u, 15);
        }
    }
    tcomp_bw_flush(&bw);
    size_t payload = tcomp_bw_size(&bw);
    size_t n = V1_HEADER + payload + V1_TRAILER;
    uint8_t *buf = malloc(n);
    if (buf != NULL) {
        put_header(buf, TCOMP_METHOD_LZ77, size);
        if (payload > 0) {
            memcpy(buf + V1_HEADER, tcomp_bw_data(&bw), payload);
        }
        put_trailer(buf + V1_HEADER + payload, crc);
        *out_len = n;
    }
    tcomp_bw_free(&bw);
    return buf;
}

/* Decompress `file` and compare the output with `expected`. */
static tcomp_status decompress_and_compare(const uint8_t *file, size_t len, const uint8_t *expected,
                                           size_t expected_len, int *matches) {
    FILE *in = file_with(file, len);
    FILE *out = tmpfile();
    tcomp_status st = (in && out) ? tcomp_decompress_stream(in, out) : TCOMP_ERR_IO;
    size_t got_len = 0;
    uint8_t *got = (st == TCOMP_OK) ? slurp(out, &got_len) : NULL;
    *matches = got != NULL && got_len == expected_len &&
               (expected_len == 0 || memcmp(got, expected, expected_len) == 0);
    free(got);
    if (in) fclose(in);
    if (out) fclose(out);
    return st;
}

/* Hand-build a HUFFMAN file: header, 4-bit code lengths (lengths[s] for
 * s = 0..255), `data`, then a trailer holding a CRC of 0. Every test that
 * uses it expects the decoder to fail before the CRC is compared.
 * buf needs HUFF_HEADER + data_len + V1_TRAILER bytes. Returns the length. */
enum { HUFF_HEADER = V1_HEADER + 128 };
static size_t build_huffman_file(uint8_t *buf, uint64_t size, const uint8_t lengths[256],
                                 const uint8_t *data, size_t data_len) {
    put_header(buf, TCOMP_METHOD_HUFFMAN, size);
    for (int s = 0; s < 256; s += 2) {
        buf[V1_HEADER + s / 2] = (uint8_t)(lengths[s] << 4 | lengths[s + 1]);
    }
    if (data_len > 0) { /* memcpy(dst, NULL, 0) is still undefined behaviour */
        memcpy(buf + HUFF_HEADER, data, data_len);
    }
    put_trailer(buf + HUFF_HEADER + data_len, 0);
    return HUFF_HEADER + data_len + V1_TRAILER;
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

/* Compress with AUTO; check it chose `expected` and that its output is
 * exactly as small as the best of the three forced methods. */
static int auto_picks(const uint8_t *data, size_t len, tcomp_method expected) {
    const tcomp_method forced[] = {TCOMP_METHOD_STORE, TCOMP_METHOD_HUFFMAN, TCOMP_METHOD_LZ77,
                                   TCOMP_METHOD_LZH};
    size_t smallest = SIZE_MAX, auto_len = 0;
    for (int m = 0; m < 4; m++) {
        size_t n = 0;
        uint8_t *out = compress_bytes(data, len, forced[m], &n);
        if (out == NULL) {
            return 0;
        }
        smallest = n < smallest ? n : smallest;
        free(out);
    }
    uint8_t *out = compress_bytes(data, len, TCOMP_METHOD_AUTO, &auto_len);
    int ok = out != NULL && out[5] == expected && auto_len == smallest;
    free(out);
    return ok;
}

TEST(test_auto_picks_smallest_method) {
    uint8_t buf[4000];
    uint32_t x = 2024;

    /* Repeated phrases: LZ77 removes the repeats, Huffman shrinks the rest. */
    for (size_t i = 0; i < sizeof buf; i++) {
        buf[i] = "the cat sat on the mat. "[i % 24];
    }
    CHECK(auto_picks(buf, sizeof buf, TCOMP_METHOD_LZH));

    /* Random bytes from a 16-letter alphabet: no repeats worth a match, but
     * 4 bits per byte suffice, so Huffman wins. */
    for (size_t i = 0; i < sizeof buf; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = (uint8_t)('a' + (x >> 16) % 16);
    }
    CHECK(auto_picks(buf, sizeof buf, TCOMP_METHOD_HUFFMAN));

    /* Uniform random bytes: nothing helps, so STORE. */
    for (size_t i = 0; i < sizeof buf; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = (uint8_t)(x >> 24);
    }
    CHECK(auto_picks(buf, sizeof buf, TCOMP_METHOD_STORE));

    /* Tiny input: every method's overhead outweighs its gain. */
    CHECK(auto_picks((const uint8_t *)"hello", 5, TCOMP_METHOD_STORE));
    CHECK(auto_picks(NULL, 0, TCOMP_METHOD_STORE));
}

TEST(test_huffman_exact_size) {
    /* 1001 copies of one byte: 1-bit codes. Payload = ceil((1024 length-table
     * bits + 1001 data bits) / 8) = 254 bytes, between header and trailer. */
    uint8_t same[1001];
    memset(same, 'A', sizeof same);
    size_t out_len = 0;
    uint8_t *out = compress_bytes(same, sizeof same, TCOMP_METHOD_HUFFMAN, &out_len);
    CHECK(out != NULL && out_len == V1_HEADER + 254 + V1_TRAILER);
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
    CHECK(decompress_bytes("TCMP\1", 5) == TCOMP_ERR_TRUNCATED);
    /* Version and method are valid, but the size field is cut short. */
    const uint8_t h[] = {'T', 'C', 'M', 'P', 1, TCOMP_METHOD_LZH, 0, 42, 0, 0};
    CHECK(decompress_bytes(h, sizeof h) == TCOMP_ERR_TRUNCATED);
}

TEST(test_rejects_future_version) {
    const uint8_t h[] = {'T', 'C', 'M', 'P', 99, TCOMP_METHOD_STORE};
    CHECK(decompress_bytes(h, sizeof h) == TCOMP_ERR_BAD_VERSION);
}

TEST(test_rejects_version_0) {
    /* Version 0 (tcomp 0.x) had no checksum. Accepting it would let a single
     * flipped bit in the version byte turn a damaged v1 file into a "valid"
     * v0 one, so 1.0 rejects it. Re-compress such files with tcomp 0.5. */
    const uint8_t v0_store[] = {'T', 'C', 'M', 'P', 0, TCOMP_METHOD_STORE, 'h', 'i'};
    CHECK(decompress_bytes(v0_store, sizeof v0_store) == TCOMP_ERR_BAD_VERSION);
}

TEST(test_rejects_nonzero_flags) {
    uint8_t f[V1_HEADER + V1_TRAILER];
    put_header(f, TCOMP_METHOD_STORE, 0);
    put_trailer(f + V1_HEADER, 0);
    CHECK(decompress_bytes(f, sizeof f) == TCOMP_OK); /* the empty file */
    for (int bit = 0; bit < 8; bit++) {
        f[6] = (uint8_t)(1u << bit);
        CHECK(decompress_bytes(f, sizeof f) == TCOMP_ERR_BAD_VERSION);
    }
}

TEST(test_rejects_unknown_method) {
    const uint8_t h[] = {'T', 'C', 'M', 'P', 1, 200};
    CHECK(decompress_bytes(h, sizeof h) == TCOMP_ERR_BAD_METHOD);
}

/* ---- HUFFMAN payload rejects bad input ---------------------------------- */

TEST(test_huffman_rejects_truncated_fields) {
    const uint8_t short_size[] = {'T', 'C', 'M', 'P', 1, TCOMP_METHOD_HUFFMAN, 0, 1, 0};
    CHECK(decompress_bytes(short_size, sizeof short_size) == TCOMP_ERR_TRUNCATED);

    uint8_t buf[HUFF_HEADER + 16];
    uint8_t lengths[256] = {0};
    lengths['A'] = 1;
    size_t n = build_huffman_file(buf, 1, lengths, NULL, 0);
    CHECK(decompress_bytes(buf, n - 60) == TCOMP_ERR_TRUNCATED); /* cut inside table */
}

TEST(test_huffman_rejects_impossible_lengths) {
    uint8_t buf[HUFF_HEADER + 16];
    uint8_t lengths[256];
    memset(lengths, 1, sizeof lengths); /* 256 one-bit codes: oversubscribed */
    const uint8_t data[] = {0x00};
    size_t n = build_huffman_file(buf, 1, lengths, data, 1);
    CHECK(decompress_bytes(buf, n) == TCOMP_ERR_CORRUPT);
}

TEST(test_huffman_rejects_unused_code) {
    uint8_t buf[HUFF_HEADER + 16];
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
    uint8_t buf[HUFF_HEADER + 16];
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
    uint8_t *buf = malloc(HUFF_HEADER + DATA_LEN + V1_TRAILER);
    uint8_t *zeros = calloc(DATA_LEN, 1);
    CHECK(buf != NULL && zeros != NULL);
    if (buf == NULL || zeros == NULL) {
        free(buf);
        free(zeros);
        return;
    }
    uint8_t lengths[256] = {0};
    lengths['A'] = 1;
    size_t n = build_huffman_file(buf, (uint64_t)DATA_LEN * 8 + 1, lengths, zeros, DATA_LEN);
    free(zeros);

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

    /* The last payload byte (before the 4-byte trailer) holds 1 data bit and
     * 7 padding bits; padding must be zero. */
    good[len - 5] |= 0x01;
    CHECK(decompress_bytes(good, len) == TCOMP_ERR_CORRUPT);
    good[len - 5] &= (uint8_t)~0x01;

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

/* ---- LZ77 payload ------------------------------------------------------- */

TEST(test_lz77_long_distances_across_buffer_refills) {
    /* 32 KB of pseudo-random literals, then 300 KB copied from exactly
     * 32768 bytes back: every match reaches the far edge of the window,
     * and the decoder's 96 KB buffer slides several times. */
    enum { LITERALS = TCOMP_LZ77_WINDOW, MATCHES = 1200 };
    size_t count = LITERALS + MATCHES;
    tcomp_lz77_token *tokens = malloc(count * sizeof *tokens);
    size_t total = LITERALS + (size_t)MATCHES * 250;
    uint8_t *expected = malloc(total);
    CHECK(tokens != NULL && expected != NULL);
    if (tokens == NULL || expected == NULL) {
        free(tokens);
        free(expected);
        return;
    }
    uint32_t x = 99;
    for (size_t i = 0; i < LITERALS; i++) {
        x = x * 1103515245u + 12345u;
        tokens[i].length = (uint16_t)(x >> 24);
        tokens[i].distance = 0;
    }
    for (size_t i = LITERALS; i < count; i++) {
        tokens[i].length = 250;
        tokens[i].distance = TCOMP_LZ77_WINDOW;
    }
    size_t n = 0;
    CHECK(tcomp_lz77_expand(tokens, count, expected, total, &n) == TCOMP_OK && n == total);

    size_t file_len = 0;
    uint8_t *file =
        build_lz77_file(tokens, count, total, tcomp_crc32_update(0, expected, total), &file_len);
    int matches = 0;
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(decompress_and_compare(file, file_len, expected, total, &matches) == TCOMP_OK);
        CHECK(matches);
    }
    free(file);
    free(tokens);
    free(expected);
}

TEST(test_lz77_rejects_invalid_tokens) {
    size_t len = 0;
    int matches;
    /* A match before any output exists. */
    const tcomp_lz77_token first_match[] = {{3, 1}};
    uint8_t *f = build_lz77_file(first_match, 1, 3, 0, &len);
    CHECK(f && decompress_and_compare(f, len, NULL, 0, &matches) == TCOMP_ERR_CORRUPT);
    free(f);

    /* Reaching further back than the output so far. */
    const tcomp_lz77_token too_far[] = {{'a', 0}, {'b', 0}, {3, 3}};
    f = build_lz77_file(too_far, 3, 5, 0, &len);
    CHECK(f && decompress_and_compare(f, len, NULL, 0, &matches) == TCOMP_ERR_CORRUPT);
    free(f);

    /* A match running past the declared size. */
    const tcomp_lz77_token overrun[] = {{'a', 0}, {10, 1}};
    f = build_lz77_file(overrun, 2, 5, 0, &len);
    CHECK(f && decompress_and_compare(f, len, NULL, 0, &matches) == TCOMP_ERR_CORRUPT);
    free(f);

    /* Fewer tokens than the size needs. */
    const tcomp_lz77_token short_stream[] = {{'a', 0}, {'b', 0}};
    f = build_lz77_file(short_stream, 2, 3, 0, &len);
    CHECK(f && decompress_and_compare(f, len, NULL, 0, &matches) == TCOMP_ERR_TRUNCATED);
    free(f);

    /* A size no token stream of this length could produce: caught up front. */
    f = build_lz77_file(short_stream, 2, 1000000, 0, &len);
    CHECK(f && decompress_and_compare(f, len, NULL, 0, &matches) == TCOMP_ERR_TRUNCATED);
    free(f);
}

TEST(test_lz77_rejects_trailing_data) {
    const tcomp_lz77_token ab[] = {{'a', 0}, {'b', 0}};
    size_t len = 0;
    int matches = 0;
    uint8_t *f = build_lz77_file(ab, 2, 2, tcomp_crc32_update(0, "ab", 2), &len);
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    CHECK(decompress_and_compare(f, len, (const uint8_t *)"ab", 2, &matches) == TCOMP_OK);
    CHECK(matches);
    /* 18 data bits leave 6 padding bits in the last payload byte (just
     * before the 4-byte trailer); they must be 0. */
    f[len - 5] |= 0x01;
    CHECK(decompress_and_compare(f, len, NULL, 0, &matches) == TCOMP_ERR_CORRUPT);
    free(f);
}

/* ---- format v1: layout, checksum, corruption ----------------------------- */

TEST(test_v1_layout_exact_bytes) {
    /* Pin every byte of a small file against FORMAT.md. */
    size_t len = 0;
    uint8_t *f = compress_bytes((const uint8_t *)"hello", 5, TCOMP_METHOD_STORE, &len);
    const uint8_t expected[] = {
        'T',  'C',  'M',  'P', 1,   0, 0,    /* magic, version 1, STORE, flags */
        5,    0,    0,    0,   0,   0, 0, 0, /* original size, little-endian */
        'h',  'e',  'l',  'l', 'o',          /* payload */
        0x86, 0xA6, 0x10, 0x36};             /* CRC-32("hello") = 0x3610A686, LE */
    CHECK(f != NULL && len == sizeof expected && memcmp(f, expected, len) == 0);
    free(f);

    /* Every method: header names it and the size; trailer holds the CRC. */
    const tcomp_method methods[] = {TCOMP_METHOD_STORE, TCOMP_METHOD_HUFFMAN, TCOMP_METHOD_LZ77,
                                    TCOMP_METHOD_LZH};
    const char *text = "abracadabra abracadabra abracadabra";
    size_t tlen = strlen(text);
    uint32_t crc = tcomp_crc32_update(0, text, tlen);
    for (int m = 0; m < 4; m++) {
        f = compress_bytes((const uint8_t *)text, tlen, methods[m], &len);
        CHECK(f != NULL && len >= V1_HEADER + V1_TRAILER);
        if (f != NULL) {
            CHECK(f[4] == 1 && f[5] == methods[m] && f[6] == 0);
            CHECK(f[7] == tlen && f[8] == 0 && f[14] == 0);
            uint32_t stored = (uint32_t)f[len - 4] | (uint32_t)f[len - 3] << 8 |
                              (uint32_t)f[len - 2] << 16 | (uint32_t)f[len - 1] << 24;
            CHECK(stored == crc);
        }
        free(f);
    }
}

TEST(test_checksum_mismatch_is_reported) {
    const char *text = "the checksum covers the original data, not the payload";
    size_t tlen = strlen(text);
    const tcomp_method methods[] = {TCOMP_METHOD_STORE, TCOMP_METHOD_HUFFMAN, TCOMP_METHOD_LZ77,
                                    TCOMP_METHOD_LZH};
    for (int m = 0; m < 4; m++) {
        size_t len = 0;
        uint8_t *f = compress_bytes((const uint8_t *)text, tlen, methods[m], &len);
        CHECK(f != NULL);
        if (f == NULL) {
            continue;
        }
        CHECK(decompress_bytes(f, len) == TCOMP_OK);
        f[len - 1] ^= 0x80; /* damage the stored CRC itself */
        CHECK(decompress_bytes(f, len) == TCOMP_ERR_CHECKSUM);
        free(f);
    }
    /* STORE has no structure to break: damaged data is caught by the CRC alone. */
    size_t len = 0;
    uint8_t *f = compress_bytes((const uint8_t *)text, tlen, TCOMP_METHOD_STORE, &len);
    CHECK(f != NULL);
    if (f != NULL) {
        f[V1_HEADER + 3] ^= 0x01;
        CHECK(decompress_bytes(f, len) == TCOMP_ERR_CHECKSUM);
        free(f);
    }
}

TEST(test_store_rejects_wrong_length) {
    size_t len = 0;
    uint8_t *f = compress_bytes((const uint8_t *)"0123456789", 10, TCOMP_METHOD_STORE, &len);
    uint8_t *longer = malloc(len + 1);
    CHECK(f != NULL && longer != NULL);
    if (f != NULL && longer != NULL) {
        memcpy(longer, f, len);
        longer[len] = 0;
        CHECK(decompress_bytes(longer, len + 1) == TCOMP_ERR_CORRUPT); /* data after trailer */
        CHECK(decompress_bytes(f, len - 1) == TCOMP_ERR_TRUNCATED);    /* trailer cut */
        CHECK(decompress_bytes(f, len - 6) == TCOMP_ERR_TRUNCATED);    /* payload cut */
        CHECK(decompress_bytes(f, V1_HEADER) == TCOMP_ERR_TRUNCATED);  /* header only */
    }
    free(f);
    free(longer);
}

TEST(test_size_field_uses_all_64_bits) {
    /* Size 2^32 + 5 with only 5 bytes of data. A decoder that read just
     * the low 32 bits would see size 5 and accept the file. */
    uint8_t f[V1_HEADER + 5 + V1_TRAILER];
    put_header(f, TCOMP_METHOD_STORE, ((uint64_t)1 << 32) + 5);
    memcpy(f + V1_HEADER, "hello", 5);
    put_trailer(f + V1_HEADER + 5, tcomp_crc32_update(0, "hello", 5));
    CHECK(decompress_bytes(f, sizeof f) == TCOMP_ERR_TRUNCATED);
    put_header(f, TCOMP_METHOD_STORE, 5);
    CHECK(decompress_bytes(f, sizeof f) == TCOMP_OK);
}

/* Decode `len` bytes from memory: 0 rejected, 1 decoded to `expected`,
 * 2 decoded "successfully" to something else (the failure we must prevent). */
static int decode_outcome(const uint8_t *file, size_t len, const uint8_t *expected,
                          size_t expected_len) {
    int matches = 0;
    tcomp_status st = decompress_and_compare(file, len, expected, expected_len, &matches);
    return st != TCOMP_OK ? 0 : matches ? 1 : 2;
}

TEST(test_every_bit_flip_and_truncation_is_caught) {
    /* The M6 acceptance test. For each method, flip every bit of a
     * compressed file in turn, and cut it at every length: the decoder must
     * either fail or return exactly the original. Before the CRC (format 0),
     * 48-80% of flips silently decoded to wrong data (docs/BENCHMARKS.md). */
    const char *text = "It was the best of times, it was the worst of times, it was the age "
                       "of wisdom, it was the age of foolishness, it was the epoch of belief, "
                       "it was the epoch of incredulity, it was the season of Light, it was "
                       "the season of Darkness.";
    size_t tlen = strlen(text);
    const uint8_t *orig = (const uint8_t *)text;
    const tcomp_method methods[] = {TCOMP_METHOD_STORE, TCOMP_METHOD_HUFFMAN, TCOMP_METHOD_LZ77,
                                    TCOMP_METHOD_LZH};
    for (int m = 0; m < 4; m++) {
        size_t len = 0;
        uint8_t *f = compress_bytes(orig, tlen, methods[m], &len);
        CHECK(f != NULL);
        if (f == NULL) {
            continue;
        }
        int wrong = 0, rejected = 0;
        for (size_t bit = 0; bit < len * 8; bit++) {
            f[bit / 8] ^= (uint8_t)(0x80u >> (bit % 8));
            int outcome = decode_outcome(f, len, orig, tlen);
            wrong += outcome == 2;
            rejected += outcome == 0;
            f[bit / 8] ^= (uint8_t)(0x80u >> (bit % 8));
        }
        int cut_accepted = 0;
        for (size_t cut = 0; cut < len; cut++) {
            cut_accepted += decode_outcome(f, cut, orig, tlen) != 0;
        }
        CHECK(wrong == 0);
        CHECK(cut_accepted == 0);
        CHECK(rejected > (int)(len * 8) * 9 / 10); /* the test really flipped bits */
        CHECK(decode_outcome(f, len, orig, tlen) == 1);
        free(f);
    }
}

/* ---- info and verify ---------------------------------------------------- */

static tcomp_status info_of(const uint8_t *file, size_t len, tcomp_info *info) {
    FILE *in = file_with(file, len);
    tcomp_status st = in ? tcomp_read_info(in, info) : TCOMP_ERR_IO;
    if (in) fclose(in);
    return st;
}

TEST(test_read_info) {
    const char *text = "info reads the header and the trailer, nothing in between";
    size_t tlen = strlen(text);
    const tcomp_method methods[] = {TCOMP_METHOD_STORE, TCOMP_METHOD_HUFFMAN, TCOMP_METHOD_LZ77,
                                    TCOMP_METHOD_LZH};
    for (int m = 0; m < 4; m++) {
        size_t len = 0;
        uint8_t *f = compress_bytes((const uint8_t *)text, tlen, methods[m], &len);
        tcomp_info info;
        memset(&info, 0xAA, sizeof info);
        CHECK(f != NULL && info_of(f, len, &info) == TCOMP_OK);
        CHECK(info.version == 1);
        CHECK(info.method == methods[m]);
        CHECK(info.original_size == tlen);
        CHECK(info.compressed_size == len);
        CHECK(info.crc32 == tcomp_crc32_update(0, text, tlen));
        free(f);
    }

    /* info reads in 64 KB chunks. 65534 stored bytes + 4 trailer bytes put
     * the last 2 trailer bytes alone in the final chunk: the trailer must
     * be stitched together across the chunk boundary. */
    enum { SPLIT = 65534 };
    uint8_t *big = malloc(SPLIT);
    CHECK(big != NULL);
    if (big != NULL) {
        for (size_t i = 0; i < SPLIT; i++) {
            big[i] = (uint8_t)(i * 13u);
        }
        size_t len = 0;
        uint8_t *f = compress_bytes(big, SPLIT, TCOMP_METHOD_STORE, &len);
        tcomp_info split_info;
        CHECK(f != NULL && info_of(f, len, &split_info) == TCOMP_OK);
        CHECK(split_info.crc32 == tcomp_crc32_update(0, big, SPLIT));
        free(f);
        free(big);
    }

    tcomp_info info;
    uint8_t empty[V1_HEADER + V1_TRAILER];
    put_header(empty, TCOMP_METHOD_STORE, 0);
    put_trailer(empty + V1_HEADER, 0);
    CHECK(info_of(empty, sizeof empty, &info) == TCOMP_OK && info.original_size == 0);
    CHECK(info_of(empty, V1_HEADER + 3, &info) == TCOMP_ERR_TRUNCATED); /* no full trailer */
    put_header(empty, TCOMP_METHOD_STORE, 1); /* claims a byte that is not there */
    CHECK(info_of(empty, sizeof empty, &info) == TCOMP_ERR_TRUNCATED);
    put_header(empty, TCOMP_METHOD_STORE, 0);
    uint8_t too_long[sizeof empty + 1] = {0};
    memcpy(too_long, empty, sizeof empty); /* one byte more than size + trailer */
    CHECK(info_of(too_long, sizeof too_long, &info) == TCOMP_ERR_CORRUPT);
    CHECK(info_of((const uint8_t *)"PK\3\4 a zip file", 15, &info) == TCOMP_ERR_BAD_MAGIC);
}

TEST(test_verify_stream) {
    size_t len = 0;
    uint8_t *f = compress_bytes((const uint8_t *)"verify me", 9, TCOMP_METHOD_HUFFMAN, &len);
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    FILE *in = file_with(f, len);
    CHECK(in != NULL && tcomp_verify_stream(in) == TCOMP_OK);
    if (in) fclose(in);
    f[len - 2] ^= 0x10;
    in = file_with(f, len);
    CHECK(in != NULL && tcomp_verify_stream(in) == TCOMP_ERR_CHECKSUM);
    if (in) fclose(in);
    free(f);
}

TEST(test_method_names) {
    CHECK(strcmp(tcomp_method_name(TCOMP_METHOD_STORE), "store") == 0);
    CHECK(strcmp(tcomp_method_name(TCOMP_METHOD_HUFFMAN), "huffman") == 0);
    CHECK(strcmp(tcomp_method_name(TCOMP_METHOD_LZ77), "lz77") == 0);
    CHECK(strcmp(tcomp_method_name(TCOMP_METHOD_LZH), "lzh") == 0);
    CHECK(strcmp(tcomp_method_name(TCOMP_METHOD_AUTO), "auto") == 0);
    CHECK(strcmp(tcomp_method_name((tcomp_method)42), "unknown") == 0);
}

TEST(test_null_arguments) {
    FILE *f = tmpfile();
    CHECK(tcomp_compress_stream(NULL, f, TCOMP_METHOD_AUTO) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_compress_stream(f, NULL, TCOMP_METHOD_AUTO) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_decompress_stream(NULL, f) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_decompress_stream(f, NULL) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_verify_stream(NULL) == TCOMP_ERR_INVALID_ARG);
    tcomp_info info;
    CHECK(tcomp_read_info(NULL, &info) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_read_info(f, NULL) == TCOMP_ERR_INVALID_ARG);
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

    RUN(test_auto_picks_smallest_method);
    RUN(test_huffman_exact_size);
    RUN(test_compress_rejects_unknown_method);

    RUN(test_rejects_empty_input);
    RUN(test_rejects_bad_magic);
    RUN(test_rejects_short_bad_magic);
    RUN(test_rejects_truncated_header);
    RUN(test_rejects_future_version);
    RUN(test_rejects_version_0);
    RUN(test_rejects_nonzero_flags);
    RUN(test_rejects_unknown_method);

    RUN(test_huffman_rejects_truncated_fields);
    RUN(test_huffman_rejects_impossible_lengths);
    RUN(test_huffman_rejects_unused_code);
    RUN(test_huffman_rejects_huge_size_quickly);
    RUN(test_huffman_validates_size_before_writing);
    RUN(test_huffman_rejects_trailing_data);

    RUN(test_lz77_long_distances_across_buffer_refills);
    RUN(test_lz77_rejects_invalid_tokens);
    RUN(test_lz77_rejects_trailing_data);

    RUN(test_v1_layout_exact_bytes);
    RUN(test_checksum_mismatch_is_reported);
    RUN(test_store_rejects_wrong_length);
    RUN(test_size_field_uses_all_64_bits);
    RUN(test_every_bit_flip_and_truncation_is_caught);

    RUN(test_read_info);
    RUN(test_verify_stream);
    RUN(test_method_names);

    RUN(test_null_arguments);
}
