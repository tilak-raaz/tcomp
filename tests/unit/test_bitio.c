/* Unit tests for bit-level I/O (src/bitio.c).
 *
 * Three kinds of test:
 *   1. Exact bytes: hand-computed expected output, which pins down the
 *      MSB-first bit order so it can never change by accident.
 *   2. Edge and error cases: zero widths, 32-bit fields, truncation, bad args.
 *   3. Randomized roundtrips: thousands of random-width fields written and
 *      read back, with fixed seeds so any failure is reproducible.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tcomp/bitio.h"
#include "test.h"

/* ---- helpers ------------------------------------------------------------ */

/* 1 if the writer's completed bytes equal `expected`. */
static int bytes_equal(const tcomp_bitwriter *bw, const uint8_t *expected, size_t len) {
    return tcomp_bw_size(bw) == len && (len == 0 || memcmp(tcomp_bw_data(bw), expected, len) == 0);
}

/* xorshift32: tiny deterministic PRNG so random tests are reproducible. */
static uint32_t next_random(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t low_mask(unsigned nbits) {
    return nbits == 32 ? UINT32_MAX : ((uint32_t)1 << nbits) - 1;
}

/* ---- writer: exact output ----------------------------------------------- */

TEST(test_writer_empty) {
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    CHECK(tcomp_bw_flush(&bw) == TCOMP_OK);
    CHECK(tcomp_bw_size(&bw) == 0);
    CHECK(tcomp_bw_bit_count(&bw) == 0);
    tcomp_bw_free(&bw);
    tcomp_bw_free(&bw); /* freeing twice must be safe */
}

TEST(test_writer_single_bits_are_msb_first) {
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    CHECK(tcomp_bw_write_bit(&bw, 1) == TCOMP_OK);
    CHECK(tcomp_bw_write_bit(&bw, 0) == TCOMP_OK);
    CHECK(tcomp_bw_write_bit(&bw, 1) == TCOMP_OK);
    CHECK(tcomp_bw_bit_count(&bw) == 3);
    CHECK(tcomp_bw_size(&bw) == 0); /* partial byte not visible before flush */
    CHECK(tcomp_bw_flush(&bw) == TCOMP_OK);
    CHECK(tcomp_bw_bit_count(&bw) == 8);
    const uint8_t expected[] = {0xA0}; /* 101 then five 0 pad bits */
    CHECK(bytes_equal(&bw, expected, sizeof expected));
    tcomp_bw_free(&bw);
}

TEST(test_writer_mixed_widths) {
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    CHECK(tcomp_bw_write_bits(&bw, 0x3, 2) == TCOMP_OK);  /* 11 */
    CHECK(tcomp_bw_write_bits(&bw, 0x0, 1) == TCOMP_OK);  /* 0 */
    CHECK(tcomp_bw_write_bits(&bw, 0x1F, 5) == TCOMP_OK); /* 11111 -> byte 11011111 */
    CHECK(tcomp_bw_write_bits(&bw, 0x5, 3) == TCOMP_OK);  /* 101   -> byte 101_00000 */
    CHECK(tcomp_bw_flush(&bw) == TCOMP_OK);
    const uint8_t expected[] = {0xDF, 0xA0};
    CHECK(bytes_equal(&bw, expected, sizeof expected));
    tcomp_bw_free(&bw);
}

TEST(test_writer_32_bit_fields) {
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    CHECK(tcomp_bw_write_bits(&bw, 0xDEADBEEF, 32) == TCOMP_OK);
    const uint8_t aligned[] = {0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(bytes_equal(&bw, aligned, sizeof aligned));
    tcomp_bw_free(&bw);

    /* Same value starting 1 bit in: 1 + DEADBEEF = 0x1DEADBEEF, 33 bits,
     * padded to 40 bits = 0x1DEADBEEF << 7. */
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    CHECK(tcomp_bw_write_bit(&bw, 1) == TCOMP_OK);
    CHECK(tcomp_bw_write_bits(&bw, 0xDEADBEEF, 32) == TCOMP_OK);
    CHECK(tcomp_bw_flush(&bw) == TCOMP_OK);
    const uint8_t shifted[] = {0xEF, 0x56, 0xDF, 0x77, 0x80};
    CHECK(bytes_equal(&bw, shifted, sizeof shifted));
    tcomp_bw_free(&bw);
}

TEST(test_writer_flush_then_continue) {
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    CHECK(tcomp_bw_write_bit(&bw, 1) == TCOMP_OK);
    CHECK(tcomp_bw_flush(&bw) == TCOMP_OK);
    CHECK(tcomp_bw_flush(&bw) == TCOMP_OK); /* second flush: already aligned, no-op */
    CHECK(tcomp_bw_write_bits(&bw, 0xFF, 8) == TCOMP_OK);
    const uint8_t expected[] = {0x80, 0xFF};
    CHECK(bytes_equal(&bw, expected, sizeof expected));
    tcomp_bw_free(&bw);
}

TEST(test_writer_buffer_grows) {
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 1) == TCOMP_OK); /* tiny start forces many reallocs */
    for (uint32_t i = 0; i < 10000; i++) {
        CHECK(tcomp_bw_write_bits(&bw, i & 0xFF, 8) == TCOMP_OK);
    }
    CHECK(tcomp_bw_size(&bw) == 10000);
    int all_match = 1;
    for (size_t i = 0; i < 10000; i++) {
        all_match &= tcomp_bw_data(&bw)[i] == (uint8_t)(i & 0xFF);
    }
    CHECK(all_match);
    tcomp_bw_free(&bw);
}

/* ---- writer: edge and error cases --------------------------------------- */

TEST(test_writer_zero_width) {
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    CHECK(tcomp_bw_write_bits(&bw, 0, 0) == TCOMP_OK);
    CHECK(tcomp_bw_bit_count(&bw) == 0);
    CHECK(tcomp_bw_write_bits(&bw, 1, 0) == TCOMP_ERR_INVALID_ARG); /* 1 doesn't fit in 0 bits */
    tcomp_bw_free(&bw);
}

TEST(test_writer_rejects_bad_arguments) {
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    CHECK(tcomp_bw_write_bits(&bw, 0, 33) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_bw_write_bits(&bw, 4, 2) == TCOMP_ERR_INVALID_ARG); /* 0b100 needs 3 bits */
    CHECK(tcomp_bw_write_bit(&bw, 2) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_bw_bit_count(&bw) == 0); /* failed writes change nothing */
    CHECK(tcomp_bw_write_bits(NULL, 0, 1) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_bw_init(NULL, 0) == TCOMP_ERR_INVALID_ARG);
    tcomp_bw_free(&bw);
    tcomp_bw_free(NULL);
}

/* ---- reader ------------------------------------------------------------- */

TEST(test_reader_exact_bits) {
    const uint8_t data[] = {0xA5, 0x0F}; /* 1010 0101  0000 1111 */
    tcomp_bitreader br;
    uint32_t v = 0;
    CHECK(tcomp_br_init(&br, data, sizeof data) == TCOMP_OK);
    CHECK(tcomp_br_bits_remaining(&br) == 16);
    CHECK(tcomp_br_read_bits(&br, 1, &v) == TCOMP_OK && v == 1);   /* 1 */
    CHECK(tcomp_br_read_bits(&br, 3, &v) == TCOMP_OK && v == 2);   /* 010 */
    CHECK(tcomp_br_read_bits(&br, 4, &v) == TCOMP_OK && v == 5);   /* 0101 */
    CHECK(tcomp_br_read_bits(&br, 8, &v) == TCOMP_OK && v == 0xF); /* 00001111 */
    CHECK(tcomp_br_bits_remaining(&br) == 0);
}

TEST(test_reader_truncation_leaves_position_unchanged) {
    const uint8_t data[] = {0xFF};
    tcomp_bitreader br;
    uint32_t v = 12345;
    unsigned bit = 7;
    CHECK(tcomp_br_init(&br, data, sizeof data) == TCOMP_OK);
    CHECK(tcomp_br_read_bits(&br, 9, &v) == TCOMP_ERR_TRUNCATED);
    CHECK(v == 12345); /* output untouched on error */
    CHECK(tcomp_br_bits_remaining(&br) == 8);
    CHECK(tcomp_br_read_bits(&br, 8, &v) == TCOMP_OK && v == 0xFF);
    CHECK(tcomp_br_read_bit(&br, &bit) == TCOMP_ERR_TRUNCATED);
    CHECK(bit == 7);
}

TEST(test_reader_empty_input) {
    tcomp_bitreader br;
    uint32_t v = 99;
    CHECK(tcomp_br_init(&br, NULL, 0) == TCOMP_OK);
    CHECK(tcomp_br_bits_remaining(&br) == 0);
    CHECK(tcomp_br_read_bits(&br, 1, &v) == TCOMP_ERR_TRUNCATED);
    CHECK(tcomp_br_read_bits(&br, 0, &v) == TCOMP_OK && v == 0);
    CHECK(tcomp_br_peek_bits(&br, 32) == 0);
}

TEST(test_reader_peek_pads_with_zeros) {
    const uint8_t data[] = {0xFF};
    tcomp_bitreader br;
    CHECK(tcomp_br_init(&br, data, sizeof data) == TCOMP_OK);
    CHECK(tcomp_br_peek_bits(&br, 12) == 0xFF0); /* 8 real bits + 4 zero pad bits */
    CHECK(tcomp_br_bits_remaining(&br) == 8);    /* peek does not consume */
    CHECK(tcomp_br_skip_bits(&br, 9) == TCOMP_ERR_TRUNCATED);
    CHECK(tcomp_br_skip_bits(&br, 8) == TCOMP_OK);
    CHECK(tcomp_br_peek_bits(&br, 4) == 0);
}

TEST(test_reader_align) {
    const uint8_t data[] = {0xFF, 0x80};
    tcomp_bitreader br;
    uint32_t v = 0;
    CHECK(tcomp_br_init(&br, data, sizeof data) == TCOMP_OK);
    tcomp_br_align(&br); /* already aligned: no-op */
    CHECK(tcomp_br_bits_remaining(&br) == 16);
    CHECK(tcomp_br_read_bits(&br, 3, &v) == TCOMP_OK);
    tcomp_br_align(&br);
    CHECK(tcomp_br_bits_remaining(&br) == 8);
    CHECK(tcomp_br_read_bits(&br, 1, &v) == TCOMP_OK && v == 1);
}

TEST(test_reader_rejects_bad_arguments) {
    tcomp_bitreader br;
    uint32_t v;
    const uint8_t data[] = {0};
    CHECK(tcomp_br_init(&br, NULL, 5) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_br_init(NULL, data, 1) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_br_init(&br, data, 1) == TCOMP_OK);
    CHECK(tcomp_br_read_bits(&br, 33, &v) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_br_read_bits(&br, 1, NULL) == TCOMP_ERR_INVALID_ARG);
}

/* ---- writer + reader together ------------------------------------------- */

TEST(test_32_bit_reads_at_every_bit_offset) {
    /* A 32-bit field starting at offset 7 spans 5 bytes: the widest case
     * the reader's 40-bit window must handle. */
    for (unsigned offset = 0; offset < 8; offset++) {
        tcomp_bitwriter bw;
        tcomp_bitreader br;
        uint32_t v = 0;
        CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
        CHECK(tcomp_bw_write_bits(&bw, 0, offset) == TCOMP_OK);
        CHECK(tcomp_bw_write_bits(&bw, 0xDEADBEEF, 32) == TCOMP_OK);
        CHECK(tcomp_bw_flush(&bw) == TCOMP_OK);
        CHECK(tcomp_br_init(&br, tcomp_bw_data(&bw), tcomp_bw_size(&bw)) == TCOMP_OK);
        CHECK(tcomp_br_skip_bits(&br, offset) == TCOMP_OK);
        CHECK(tcomp_br_read_bits(&br, 32, &v) == TCOMP_OK);
        CHECK(v == 0xDEADBEEF);
        tcomp_bw_free(&bw);
    }
}

TEST(test_random_roundtrip) {
    enum { FIELDS = 50000 };
    uint32_t *values = malloc(FIELDS * sizeof *values);
    uint8_t *widths = malloc(FIELDS);
    CHECK(values != NULL && widths != NULL);
    if (values == NULL || widths == NULL) {
        free(values);
        free(widths);
        return;
    }

    for (uint32_t seed = 1; seed <= 5; seed++) {
        uint32_t rng = seed * 2654435761u; /* spread small seeds apart */
        size_t total_bits = 0;
        for (size_t i = 0; i < FIELDS; i++) {
            widths[i] = (uint8_t)(next_random(&rng) % 33); /* 0..32 inclusive */
            values[i] = next_random(&rng) & low_mask(widths[i]);
            total_bits += widths[i];
        }

        tcomp_bitwriter bw;
        CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
        int writes_ok = 1;
        for (size_t i = 0; i < FIELDS; i++) {
            writes_ok &= tcomp_bw_write_bits(&bw, values[i], widths[i]) == TCOMP_OK;
        }
        CHECK(writes_ok);
        CHECK(tcomp_bw_bit_count(&bw) == total_bits);
        CHECK(tcomp_bw_flush(&bw) == TCOMP_OK);
        CHECK(tcomp_bw_size(&bw) == (total_bits + 7) / 8);

        tcomp_bitreader br;
        CHECK(tcomp_br_init(&br, tcomp_bw_data(&bw), tcomp_bw_size(&bw)) == TCOMP_OK);
        size_t mismatches = 0;
        for (size_t i = 0; i < FIELDS; i++) {
            uint32_t v = 0;
            /* Alternate between read_bits and peek+skip to test both paths. */
            if (i % 2 == 0) {
                if (tcomp_br_read_bits(&br, widths[i], &v) != TCOMP_OK) {
                    mismatches++;
                }
            } else {
                v = tcomp_br_peek_bits(&br, widths[i]);
                if (tcomp_br_skip_bits(&br, widths[i]) != TCOMP_OK) {
                    mismatches++;
                }
            }
            mismatches += v != values[i];
        }
        CHECK(mismatches == 0);
        CHECK(tcomp_br_bits_remaining(&br) < 8); /* only padding left */
        tcomp_bw_free(&bw);
    }
    free(values);
    free(widths);
}

void suite_bitio(void) {
    RUN(test_writer_empty);
    RUN(test_writer_single_bits_are_msb_first);
    RUN(test_writer_mixed_widths);
    RUN(test_writer_32_bit_fields);
    RUN(test_writer_flush_then_continue);
    RUN(test_writer_buffer_grows);
    RUN(test_writer_zero_width);
    RUN(test_writer_rejects_bad_arguments);

    RUN(test_reader_exact_bits);
    RUN(test_reader_truncation_leaves_position_unchanged);
    RUN(test_reader_empty_input);
    RUN(test_reader_peek_pads_with_zeros);
    RUN(test_reader_align);
    RUN(test_reader_rejects_bad_arguments);

    RUN(test_32_bit_reads_at_every_bit_offset);
    RUN(test_random_roundtrip);
}
