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

/* compress then decompress `data`; returns 1 if the output matches exactly. */
static int roundtrips(const uint8_t *data, size_t len) {
    FILE *in = file_with(data, len);
    FILE *packed = tmpfile();
    FILE *restored = tmpfile();
    int ok = in != NULL && packed != NULL && restored != NULL;

    ok = ok && tcomp_compress_stream(in, packed) == TCOMP_OK;
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

TEST(test_null_arguments) {
    FILE *f = tmpfile();
    CHECK(tcomp_compress_stream(NULL, f) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_compress_stream(f, NULL) == TCOMP_ERR_INVALID_ARG);
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

    RUN(test_rejects_empty_input);
    RUN(test_rejects_bad_magic);
    RUN(test_rejects_short_bad_magic);
    RUN(test_rejects_truncated_header);
    RUN(test_rejects_future_version);
    RUN(test_rejects_unknown_method);
    RUN(test_null_arguments);
}
