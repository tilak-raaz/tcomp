/* Unit tests for Huffman coding (src/huffman.c).
 *
 *   1. Known answers: a textbook frequency table (CLRS) and the canonical
 *      code example from RFC 1951, so the algorithms match the literature.
 *   2. Properties on random inputs: lengths form a complete prefix code and
 *      the average code length is within the Huffman bound H <= L < H + 1.
 *   3. Length limiting: inputs that need deep trees stay within the limit.
 *   4. Decoder: random roundtrips, and every way input can be invalid.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tcomp/bitio.h"
#include "tcomp/huffman.h"
#include "test.h"

/* ---- helpers ------------------------------------------------------------ */

static uint32_t next_random(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Kraft sum scaled by 2^MAX_BITS: equals 2^MAX_BITS exactly for a complete
 * prefix code (every bit pattern used), less for an incomplete one. */
static uint64_t kraft_scaled(const uint8_t *lengths, size_t n) {
    uint64_t sum = 0;
    for (size_t s = 0; s < n; s++) {
        if (lengths[s] > 0) {
            sum += (uint64_t)1 << (TCOMP_HUFF_MAX_BITS - lengths[s]);
        }
    }
    return sum;
}

static unsigned max_length(const uint8_t *lengths, size_t n) {
    unsigned max = 0;
    for (size_t s = 0; s < n; s++) {
        max = lengths[s] > max ? lengths[s] : max;
    }
    return max;
}

/* ---- building lengths --------------------------------------------------- */

TEST(test_lengths_textbook_example) {
    /* CLRS 3rd ed., figure 16.4: a..f. Optimal lengths are unique here. */
    const uint64_t freqs[] = {45, 13, 12, 16, 9, 5};
    const uint8_t expected[] = {1, 3, 3, 3, 4, 4};
    uint8_t lengths[6];
    CHECK(tcomp_huff_build_lengths(freqs, 6, 15, lengths) == TCOMP_OK);
    CHECK(memcmp(lengths, expected, sizeof expected) == 0);
    uint64_t cost = 0;
    for (int s = 0; s < 6; s++) {
        cost += freqs[s] * lengths[s];
    }
    CHECK(cost == 224); /* total bits, as in the book */
}

TEST(test_lengths_degenerate_inputs) {
    uint64_t freqs[4] = {0, 0, 0, 0};
    uint8_t lengths[4];
    CHECK(tcomp_huff_build_lengths(freqs, 4, 15, lengths) == TCOMP_OK);
    CHECK(max_length(lengths, 4) == 0); /* nothing used: no codes */

    freqs[2] = 99; /* one symbol: still needs a 1-bit code */
    CHECK(tcomp_huff_build_lengths(freqs, 4, 15, lengths) == TCOMP_OK);
    CHECK(lengths[0] == 0 && lengths[1] == 0 && lengths[2] == 1 && lengths[3] == 0);

    freqs[0] = 1; /* two symbols: one bit each, however skewed */
    CHECK(tcomp_huff_build_lengths(freqs, 4, 15, lengths) == TCOMP_OK);
    CHECK(lengths[0] == 1 && lengths[2] == 1 && lengths[1] == 0 && lengths[3] == 0);
}

TEST(test_lengths_are_deterministic_on_ties) {
    uint64_t freqs[8];
    for (int s = 0; s < 8; s++) {
        freqs[s] = 7; /* all equal: any balanced tree is optimal */
    }
    uint8_t a[8], b[8];
    CHECK(tcomp_huff_build_lengths(freqs, 8, 15, a) == TCOMP_OK);
    CHECK(tcomp_huff_build_lengths(freqs, 8, 15, b) == TCOMP_OK);
    CHECK(memcmp(a, b, sizeof a) == 0);
    for (int s = 0; s < 8; s++) {
        CHECK(a[s] == 3);
    }
}

TEST(test_lengths_respect_limit) {
    /* Fibonacci frequencies build the deepest possible tree: unlimited, the
     * rarest symbol would need a 24-bit code. */
    enum { N = 25 };
    uint64_t freqs[N];
    freqs[0] = 1;
    freqs[1] = 1;
    for (int s = 2; s < N; s++) {
        freqs[s] = freqs[s - 1] + freqs[s - 2];
    }
    uint8_t lengths[N];
    for (unsigned limit = 5; limit <= 15; limit += 5) {
        CHECK(tcomp_huff_build_lengths(freqs, N, limit, lengths) == TCOMP_OK);
        CHECK(max_length(lengths, N) <= limit);
        CHECK(kraft_scaled(lengths, N) == (uint64_t)1 << TCOMP_HUFF_MAX_BITS);
    }
}

TEST(test_lengths_reject_bad_arguments) {
    uint64_t freqs[3] = {1, 1, 1};
    uint8_t lengths[3];
    CHECK(tcomp_huff_build_lengths(freqs, 0, 15, lengths) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_huff_build_lengths(freqs, 3, 0, lengths) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_huff_build_lengths(freqs, 3, 16, lengths) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_huff_build_lengths(freqs, 3, 1, lengths) == TCOMP_ERR_INVALID_ARG); /* 3 > 2^1 */
    CHECK(tcomp_huff_build_lengths(NULL, 3, 15, lengths) == TCOMP_ERR_INVALID_ARG);
    CHECK(tcomp_huff_build_lengths(freqs, 3, 15, NULL) == TCOMP_ERR_INVALID_ARG);
    uint64_t overflow[2] = {UINT64_MAX, 1};
    CHECK(tcomp_huff_build_lengths(overflow, 2, 15, lengths) == TCOMP_ERR_INVALID_ARG);
}

TEST(test_lengths_within_huffman_bound) {
    /* Shannon: no code beats the entropy H. Huffman guarantees L < H + 1. */
    enum { N = 256 };
    uint64_t freqs[N];
    uint8_t lengths[N];
    int all_ok = 1;
    for (uint32_t seed = 1; seed <= 50; seed++) {
        uint32_t rng = seed * 2654435761u;
        uint64_t total = 0;
        unsigned used = 0;
        for (int s = 0; s < N; s++) {
            /* Mix of unused, rare and common symbols. */
            uint32_t r = next_random(&rng);
            freqs[s] = (r % 4 == 0) ? 0 : 1 + (r >> 8) % 1000;
            total += freqs[s];
            used += freqs[s] > 0;
        }
        if (tcomp_huff_build_lengths(freqs, N, 15, lengths) != TCOMP_OK) {
            all_ok = 0;
            continue;
        }
        double entropy = 0, avg = 0;
        for (int s = 0; s < N; s++) {
            if (freqs[s] > 0) {
                double p = (double)freqs[s] / (double)total;
                entropy -= p * log2(p);
                avg += p * lengths[s];
            }
        }
        all_ok &= avg >= entropy - 1e-9 && avg < entropy + 1;
        all_ok &= used < 2 || kraft_scaled(lengths, N) == (uint64_t)1 << TCOMP_HUFF_MAX_BITS;
    }
    CHECK(all_ok);
}

/* ---- canonical codes ---------------------------------------------------- */

TEST(test_codes_rfc1951_example) {
    /* RFC 1951, 3.2.2: symbols A..H with lengths (3,3,3,3,3,2,4,4). */
    const uint8_t lengths[] = {3, 3, 3, 3, 3, 2, 4, 4};
    const uint16_t expected[] = {0x2, 0x3, 0x4, 0x5, 0x6, 0x0, 0xE, 0xF};
    uint16_t codes[8];
    CHECK(tcomp_huff_assign_codes(lengths, 8, codes) == TCOMP_OK);
    CHECK(memcmp(codes, expected, sizeof expected) == 0);
}

TEST(test_codes_validate_lengths) {
    uint16_t codes[3];
    const uint8_t oversubscribed[] = {1, 1, 1}; /* three 1-bit codes: impossible */
    CHECK(tcomp_huff_assign_codes(oversubscribed, 3, codes) == TCOMP_ERR_CORRUPT);
    const uint8_t too_long[] = {1, 16, 0};
    CHECK(tcomp_huff_assign_codes(too_long, 3, codes) == TCOMP_ERR_CORRUPT);
    const uint8_t incomplete[] = {0, 1, 0}; /* one code, '1' unused: allowed */
    CHECK(tcomp_huff_assign_codes(incomplete, 3, codes) == TCOMP_OK && codes[1] == 0);
    CHECK(tcomp_huff_assign_codes(incomplete, 0, codes) == TCOMP_ERR_INVALID_ARG);
}

/* ---- decoder ------------------------------------------------------------ */

TEST(test_decoder_random_roundtrip) {
    /* A 300-symbol alphabet, as M4's literal/length alphabet will need. */
    enum { N = 300, COUNT = 20000 };
    uint64_t freqs[N];
    uint8_t lengths[N];
    uint16_t codes[N];
    uint16_t used[N];
    unsigned nused = 0;
    uint32_t rng = 12345;
    for (int s = 0; s < N; s++) {
        uint32_t r = next_random(&rng);
        freqs[s] = (r % 3 == 0) ? 0 : 1 + (r >> 4) % 5000;
        if (freqs[s] > 0) {
            used[nused++] = (uint16_t)s;
        }
    }
    CHECK(tcomp_huff_build_lengths(freqs, N, 15, lengths) == TCOMP_OK);
    CHECK(tcomp_huff_assign_codes(lengths, N, codes) == TCOMP_OK);

    unsigned *symbols = malloc(COUNT * sizeof *symbols);
    CHECK(symbols != NULL);
    if (symbols == NULL) {
        return;
    }
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    for (int i = 0; i < COUNT; i++) {
        symbols[i] = used[next_random(&rng) % nused];
        CHECK(tcomp_bw_write_bits(&bw, codes[symbols[i]], lengths[symbols[i]]) == TCOMP_OK);
    }
    CHECK(tcomp_bw_flush(&bw) == TCOMP_OK);

    tcomp_huff_decoder dec;
    tcomp_bitreader br;
    CHECK(tcomp_huff_decoder_init(&dec, lengths, N) == TCOMP_OK);
    CHECK(tcomp_br_init(&br, tcomp_bw_data(&bw), tcomp_bw_size(&bw)) == TCOMP_OK);
    int mismatches = 0;
    for (int i = 0; i < COUNT; i++) {
        unsigned sym = 9999;
        mismatches += tcomp_huff_decode_symbol(&dec, &br, &sym) != TCOMP_OK || sym != symbols[i];
    }
    CHECK(mismatches == 0);
    CHECK(tcomp_br_bits_remaining(&br) < 8);

    tcomp_huff_decoder_free(&dec);
    tcomp_huff_decoder_free(&dec); /* twice must be safe */
    tcomp_bw_free(&bw);
    free(symbols);
}

TEST(test_decoder_rejects_unused_code) {
    const uint8_t lengths[] = {0, 1}; /* only code '0' exists */
    const uint8_t data[] = {0x80};    /* starts with '1' */
    tcomp_huff_decoder dec;
    tcomp_bitreader br;
    unsigned sym = 77;
    CHECK(tcomp_huff_decoder_init(&dec, lengths, 2) == TCOMP_OK);
    CHECK(tcomp_br_init(&br, data, 1) == TCOMP_OK);
    CHECK(tcomp_huff_decode_symbol(&dec, &br, &sym) == TCOMP_ERR_CORRUPT);
    CHECK(sym == 77 && tcomp_br_bits_remaining(&br) == 8); /* nothing consumed */
    tcomp_huff_decoder_free(&dec);
}

TEST(test_decoder_rejects_truncated_code) {
    const uint8_t lengths[] = {1, 2, 2}; /* codes 0, 10, 11 */
    const uint8_t data[] = {0x01};       /* 0000000 then a lone '1' */
    tcomp_huff_decoder dec;
    tcomp_bitreader br;
    unsigned sym;
    CHECK(tcomp_huff_decoder_init(&dec, lengths, 3) == TCOMP_OK);
    CHECK(tcomp_br_init(&br, data, 1) == TCOMP_OK);
    for (int i = 0; i < 7; i++) {
        CHECK(tcomp_huff_decode_symbol(&dec, &br, &sym) == TCOMP_OK && sym == 0);
    }
    /* '1' + padding looks like code 10, but only 1 real bit is left. */
    CHECK(tcomp_huff_decode_symbol(&dec, &br, &sym) == TCOMP_ERR_TRUNCATED);
    CHECK(tcomp_br_bits_remaining(&br) == 1);
    tcomp_huff_decoder_free(&dec);
}

TEST(test_decoder_empty_and_invalid) {
    const uint8_t none[] = {0, 0, 0};
    const uint8_t data[] = {0xFF};
    tcomp_huff_decoder dec;
    tcomp_bitreader br;
    unsigned sym;
    CHECK(tcomp_huff_decoder_init(&dec, none, 3) == TCOMP_OK);
    CHECK(tcomp_br_init(&br, data, 1) == TCOMP_OK);
    CHECK(tcomp_huff_decode_symbol(&dec, &br, &sym) == TCOMP_ERR_CORRUPT);
    tcomp_huff_decoder_free(&dec);

    const uint8_t bad[] = {1, 1, 1};
    CHECK(tcomp_huff_decoder_init(&dec, bad, 3) == TCOMP_ERR_CORRUPT);
    tcomp_huff_decoder_free(&dec); /* safe after failed init */
    CHECK(tcomp_huff_decoder_init(NULL, none, 3) == TCOMP_ERR_INVALID_ARG);
}

void suite_huffman(void) {
    RUN(test_lengths_textbook_example);
    RUN(test_lengths_degenerate_inputs);
    RUN(test_lengths_are_deterministic_on_ties);
    RUN(test_lengths_respect_limit);
    RUN(test_lengths_reject_bad_arguments);
    RUN(test_lengths_within_huffman_bound);

    RUN(test_codes_rfc1951_example);
    RUN(test_codes_validate_lengths);

    RUN(test_decoder_random_roundtrip);
    RUN(test_decoder_rejects_unused_code);
    RUN(test_decoder_rejects_truncated_code);
    RUN(test_decoder_empty_and_invalid);
}
