/* Unit tests for CRC-32 (src/crc32.c).
 *
 * Known answers pin the exact variant (the one gzip, zlib and PNG use).
 * A bit-at-a-time reference, written straight from the definition, checks
 * the table-driven code on random data of every length and alignment, so a
 * wrong entry in the generated tables cannot go unnoticed.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tcomp/crc32.h"
#include "test.h"

/* The definition: shift each bit through the reflected polynomial. */
static uint32_t crc32_reference(const uint8_t *p, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static uint32_t crc_of(const char *s) { return tcomp_crc32_update(0, s, strlen(s)); }

TEST(test_crc32_known_answers) {
    CHECK(crc_of("") == 0x00000000u);
    CHECK(crc_of("a") == 0xE8B7BE43u);
    CHECK(crc_of("123456789") == 0xCBF43926u); /* the standard check value */
    CHECK(crc_of("The quick brown fox jumps over the lazy dog") == 0x414FA339u);
    CHECK(tcomp_crc32_update(0, NULL, 0) == 0);
}

TEST(test_crc32_zero_bytes_are_not_ignored) {
    /* The pre- and post-inversion make leading zeros count. */
    const uint8_t z[4] = {0};
    CHECK(tcomp_crc32_update(0, z, 1) == 0xD202EF8Du);
    CHECK(tcomp_crc32_update(0, z, 4) == 0x2144DF1Cu);
    CHECK(tcomp_crc32_update(0, z, 1) != tcomp_crc32_update(0, z, 4));
}

TEST(test_crc32_matches_reference) {
    /* Every length 0..300 (all slicing remainders, many full 8-byte steps)
     * at 8 starting offsets (all alignments). */
    enum { MAX_LEN = 300, OFFSETS = 8 };
    uint8_t buf[MAX_LEN + OFFSETS];
    uint32_t x = 12345;
    for (size_t i = 0; i < sizeof buf; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = (uint8_t)(x >> 24);
    }
    int mismatches = 0;
    for (size_t off = 0; off < OFFSETS; off++) {
        for (size_t len = 0; len <= MAX_LEN; len++) {
            mismatches += tcomp_crc32_update(0, buf + off, len) != crc32_reference(buf + off, len);
        }
    }
    CHECK(mismatches == 0);
}

TEST(test_crc32_every_byte_value) {
    /* Each single byte goes through table 0 alone; each byte at each of the
     * 8 positions of an 8-byte step goes through every table. */
    int mismatches = 0;
    for (unsigned b = 0; b < 256; b++) {
        uint8_t one = (uint8_t)b;
        mismatches += tcomp_crc32_update(0, &one, 1) != crc32_reference(&one, 1);
        for (int pos = 0; pos < 8; pos++) {
            uint8_t eight[8] = {0};
            eight[pos] = (uint8_t)b;
            mismatches += tcomp_crc32_update(0, eight, 8) != crc32_reference(eight, 8);
        }
    }
    CHECK(mismatches == 0);
}

TEST(test_crc32_incremental_equals_one_shot) {
    const char *s = "The quick brown fox jumps over the lazy dog";
    size_t len = strlen(s);
    uint32_t whole = tcomp_crc32_update(0, s, len);
    int mismatches = 0;
    for (size_t cut = 0; cut <= len; cut++) {
        uint32_t crc = tcomp_crc32_update(0, s, cut);
        crc = tcomp_crc32_update(crc, s + cut, len - cut);
        mismatches += crc != whole;
    }
    /* Three pieces, including empty ones. */
    uint32_t crc = tcomp_crc32_update(0, s, 0);
    crc = tcomp_crc32_update(crc, s, 10);
    crc = tcomp_crc32_update(crc, s + 10, 0);
    crc = tcomp_crc32_update(crc, s + 10, len - 10);
    CHECK(mismatches == 0);
    CHECK(crc == whole);
}

TEST(test_crc32_detects_every_single_bit_flip) {
    /* CRC-32 detects every error burst up to 32 bits long; a single flipped
     * bit is the simplest case. Check it for all 8192 bits of 1 KB. */
    enum { LEN = 1024 };
    uint8_t *buf = malloc(LEN);
    CHECK(buf != NULL);
    if (buf == NULL) {
        return;
    }
    for (size_t i = 0; i < LEN; i++) {
        buf[i] = (uint8_t)(i * 7u);
    }
    uint32_t good = tcomp_crc32_update(0, buf, LEN);
    int undetected = 0;
    for (size_t bit = 0; bit < LEN * 8; bit++) {
        buf[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        undetected += tcomp_crc32_update(0, buf, LEN) == good;
        buf[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    }
    CHECK(undetected == 0);
    free(buf);
}

void suite_crc32(void) {
    RUN(test_crc32_known_answers);
    RUN(test_crc32_zero_bytes_are_not_ignored);
    RUN(test_crc32_matches_reference);
    RUN(test_crc32_every_byte_value);
    RUN(test_crc32_incremental_equals_one_shot);
    RUN(test_crc32_detects_every_single_bit_flip);
}
