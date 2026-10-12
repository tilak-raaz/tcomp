/* Unit tests for LZ77 parsing and expansion (src/lz77.c).
 *
 *   1. Exact tokens for small inputs, so the parsing rules (min/max length,
 *      overlap, nearest-on-ties, window limit) are pinned down. Every rule is
 *      checked against all three parsers: naive, hash chains with no limit,
 *      and hash chains with the default limit.
 *   2. Differential tests: hash chains with no limit must produce exactly the
 *      naive parser's tokens, including after the chain ring wraps around.
 *   3. Expansion rejects every kind of invalid token.
 *   4. Seeded random roundtrips over many window sizes and chain limits.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tcomp/lz77.h"
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

#define LIT(c)                                                                                     \
    { (uint16_t)(c), 0 }
#define MATCH(len, dist)                                                                           \
    { (len), (dist) }

/* The parsers under test. chain == NAIVE selects the reference parser. */
enum { NAIVE = -1 };
static const long PARSERS[] = {NAIVE, TCOMP_LZ77_UNLIMITED_CHAIN, TCOMP_LZ77_DEFAULT_CHAIN};
enum { NUM_PARSERS = sizeof PARSERS / sizeof *PARSERS };

static tcomp_status parse_with(long chain, const uint8_t *data, size_t len, size_t window,
                               tcomp_lz77_tokens *t) {
    return chain == NAIVE ? tcomp_lz77_parse_naive(data, len, window, t)
                          : tcomp_lz77_parse(data, len, window, (size_t)chain, t);
}

/* Parse `s` with every parser and compare against the expected tokens. */
static int parses_to(const char *s, const tcomp_lz77_token *expected, size_t n) {
    int ok = 1;
    for (int p = 0; p < NUM_PARSERS; p++) {
        tcomp_lz77_tokens t;
        tcomp_lz77_tokens_init(&t);
        ok &= parse_with(PARSERS[p], (const uint8_t *)s, strlen(s), TCOMP_LZ77_WINDOW, &t) ==
                  TCOMP_OK &&
              t.count == n && (n == 0 || memcmp(t.items, expected, n * sizeof *expected) == 0);
        tcomp_lz77_tokens_free(&t);
    }
    return ok;
}

/* Parse, check every token is legal for `window`, expand, compare. */
static int roundtrips(const uint8_t *data, size_t len, size_t window, long chain) {
    tcomp_lz77_tokens t;
    tcomp_lz77_tokens_init(&t);
    uint8_t *out = malloc(len + 1);
    size_t out_len = 0, covered = 0;
    int ok = out != NULL && parse_with(chain, data, len, window, &t) == TCOMP_OK;
    for (size_t i = 0; ok && i < t.count; i++) {
        const tcomp_lz77_token *k = &t.items[i];
        if (k->distance == 0) {
            covered += 1;
        } else {
            ok = k->length >= TCOMP_LZ77_MIN_MATCH && k->length <= TCOMP_LZ77_MAX_MATCH &&
                 k->distance <= window && k->distance <= covered;
            covered += k->length;
        }
    }
    ok = ok && covered == len;
    ok = ok && tcomp_lz77_expand(t.items, t.count, out, len, &out_len) == TCOMP_OK;
    ok = ok && out_len == len && (len == 0 || memcmp(out, data, len) == 0);
    free(out);
    tcomp_lz77_tokens_free(&t);
    return ok;
}

/* 1 if the unlimited hash-chain parser gives exactly the naive parser's tokens. */
static int same_as_naive(const uint8_t *data, size_t len, size_t window) {
    tcomp_lz77_tokens a, b;
    tcomp_lz77_tokens_init(&a);
    tcomp_lz77_tokens_init(&b);
    int ok = tcomp_lz77_parse_naive(data, len, window, &a) == TCOMP_OK &&
             tcomp_lz77_parse(data, len, window, TCOMP_LZ77_UNLIMITED_CHAIN, &b) == TCOMP_OK &&
             a.count == b.count &&
             (a.count == 0 || memcmp(a.items, b.items, a.count * sizeof *a.items) == 0);
    tcomp_lz77_tokens_free(&a);
    tcomp_lz77_tokens_free(&b);
    return ok;
}

/* Words from a small vocabulary plus random noise: matches of every length
 * and distance. */
static void fill_wordy(uint8_t *data, size_t len, uint32_t *rng) {
    static const char *words[] = {
        "lorem ", "ipsum ", "dolor ", "sit ", "amet, ", "\n", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for (size_t i = 0; i < len;) {
        uint32_t r = next_random(rng);
        if (r % 5 == 0) {
            data[i++] = (uint8_t)(r >> 8); /* noise */
        } else {
            const char *w = words[(r >> 8) % 7];
            for (size_t k = 0; w[k] && i < len; k++) {
                data[i++] = (uint8_t)w[k];
            }
        }
    }
}

/* ---- parsing: exact tokens ---------------------------------------------- */

TEST(test_parse_repeat) {
    const tcomp_lz77_token want[] = {LIT('a'), LIT('b'), LIT('c'), MATCH(6, 3), LIT('x')};
    CHECK(parses_to("abcabcabcx", want, 5));
}

TEST(test_parse_overlapping_run) {
    /* One literal, then a match that copies from itself: run-length coding. */
    const tcomp_lz77_token want[] = {LIT('a'), MATCH(9, 1)};
    CHECK(parses_to("aaaaaaaaaa", want, 2));
}

TEST(test_parse_short_repeats_stay_literal) {
    /* "ab" repeats, but a 2-byte match costs more than 2 literals. */
    const tcomp_lz77_token want[] = {LIT('a'), LIT('b'), LIT('a'), LIT('b')};
    CHECK(parses_to("abab", want, 4));
    CHECK(parses_to("", NULL, 0));
}

TEST(test_parse_prefers_nearest_on_ties) {
    /* The last "abc" matches both 4 and 8 bytes back, equally long: take 4.
     * The trailing Z matters: without it the first 3-byte match would be
     * the longest possible and the search would stop before seeing 8. */
    const tcomp_lz77_token want[] = {LIT('a'),    LIT('b'), LIT('c'),    LIT('X'),
                                     MATCH(3, 4), LIT('Y'), MATCH(3, 4), LIT('Z')};
    CHECK(parses_to("abcXabcYabcZ", want, 8));
}

TEST(test_parse_caps_match_length) {
    uint8_t run[1000];
    memset(run, 'z', sizeof run);
    for (int p = 0; p < NUM_PARSERS; p++) {
        tcomp_lz77_tokens t;
        tcomp_lz77_tokens_init(&t);
        CHECK(parse_with(PARSERS[p], run, sizeof run, TCOMP_LZ77_WINDOW, &t) == TCOMP_OK);
        /* 'z', then 999 bytes as 258 + 258 + 258 + 225, all at distance 1. */
        CHECK(t.count == 5);
        if (t.count == 5) {
            CHECK(t.items[0].distance == 0 && t.items[0].length == 'z');
            CHECK(t.items[1].length == 258 && t.items[2].length == 258);
            CHECK(t.items[3].length == 258 && t.items[4].length == 225);
            CHECK(t.items[1].distance == 1 && t.items[4].distance == 1);
        }
        tcomp_lz77_tokens_free(&t);
    }
}

TEST(test_parse_respects_window) {
    /* "0123" repeats 17 bytes back: found with a 32-byte window, not a 16-byte one. */
    const uint8_t *s = (const uint8_t *)"0123456789abcdefg0123";
    for (int p = 0; p < NUM_PARSERS; p++) {
        tcomp_lz77_tokens t;
        tcomp_lz77_tokens_init(&t);
        CHECK(parse_with(PARSERS[p], s, 21, 16, &t) == TCOMP_OK && t.count == 21);
        tcomp_lz77_tokens_free(&t);
        CHECK(parse_with(PARSERS[p], s, 21, 32, &t) == TCOMP_OK && t.count == 18);
        if (t.count == 18) {
            CHECK(t.items[17].length == 4 && t.items[17].distance == 17);
        }
        tcomp_lz77_tokens_free(&t);
        tcomp_lz77_tokens_free(&t); /* twice must be safe */
    }
}

TEST(test_parse_rejects_bad_arguments) {
    tcomp_lz77_tokens t;
    tcomp_lz77_tokens_init(&t);
    const uint8_t d[] = "x";
    for (int p = 0; p < NUM_PARSERS; p++) {
        long c = PARSERS[p];
        CHECK(parse_with(c, d, 1, 0, &t) == TCOMP_ERR_INVALID_ARG);
        CHECK(parse_with(c, d, 1, TCOMP_LZ77_WINDOW + 1, &t) == TCOMP_ERR_INVALID_ARG);
        CHECK(parse_with(c, NULL, 1, 16, &t) == TCOMP_ERR_INVALID_ARG);
        CHECK(parse_with(c, d, 1, 16, NULL) == TCOMP_ERR_INVALID_ARG);
        CHECK(parse_with(c, NULL, 0, 16, &t) == TCOMP_OK && t.count == 0);
    }
}

/* ---- hash chains vs naive ----------------------------------------------- */

TEST(test_hash_chains_match_naive_on_random_inputs) {
    enum { MAX_LEN = 6000 };
    uint8_t *data = malloc(MAX_LEN);
    CHECK(data != NULL);
    if (data == NULL) {
        return;
    }
    const size_t windows[] = {1, 3, 64, 1000, TCOMP_LZ77_WINDOW};
    int mismatches = 0;
    for (uint32_t seed = 1; seed <= 20; seed++) {
        uint32_t rng = seed * 2654435761u;
        size_t len = next_random(&rng) % MAX_LEN;
        if (seed % 3 == 0) {
            for (size_t i = 0; i < len; i++) { /* 4-letter alphabet: long chains */
                data[i] = "ACGT"[next_random(&rng) % 4];
            }
        } else if (seed % 3 == 1) {
            fill_wordy(data, len, &rng);
        } else {
            for (size_t i = 0; i < len; i++) { /* binary: few matches */
                data[i] = (uint8_t)next_random(&rng);
            }
        }
        for (size_t w = 0; w < sizeof windows / sizeof *windows; w++) {
            mismatches += !same_as_naive(data, len, windows[w]);
        }
    }
    CHECK(mismatches == 0);
    free(data);
}

TEST(test_hash_chains_match_naive_after_ring_wraps) {
    /* 80 KB: a 20 KB random block repeated with a mutation every few hundred
     * bytes. Matches reach 20 KB back from positions far beyond the 32 KB
     * ring, so stale ring slots would show up as different tokens. */
    enum { BLOCK = 20000, LEN = 80000 };
    uint8_t *data = malloc(LEN);
    CHECK(data != NULL);
    if (data == NULL) {
        return;
    }
    uint32_t rng = 777;
    for (size_t i = 0; i < BLOCK; i++) {
        data[i] = (uint8_t)next_random(&rng);
    }
    for (size_t i = BLOCK; i < LEN; i++) {
        data[i] = next_random(&rng) % 300 == 0 ? (uint8_t)next_random(&rng) : data[i - BLOCK];
    }
    CHECK(same_as_naive(data, LEN, TCOMP_LZ77_WINDOW));
    CHECK(roundtrips(data, LEN, TCOMP_LZ77_WINDOW, TCOMP_LZ77_DEFAULT_CHAIN));
    free(data);
}

TEST(test_chain_limit_counts_candidates) {
    /* At the final "abcd", two earlier positions start with "abc": 4 bytes
     * back ("abcR", 3 bytes match) and 9 bytes back ("abcd", 4 bytes).
     * A chain of 1 only looks at the nearest; a chain of 2 finds the longer. */
    const uint8_t *s = (const uint8_t *)"abcdQabcRabcd";
    tcomp_lz77_tokens t;
    tcomp_lz77_tokens_init(&t);
    /* Tokens: a b c d Q, "abc" (3, 5), R, then the final "abcd". */
    CHECK(tcomp_lz77_parse(s, 13, TCOMP_LZ77_WINDOW, 1, &t) == TCOMP_OK && t.count == 9);
    if (t.count == 9) {
        CHECK(t.items[5].length == 3 && t.items[5].distance == 5);
        CHECK(t.items[7].length == 3 && t.items[7].distance == 4);
        CHECK(t.items[8].distance == 0 && t.items[8].length == 'd');
    }
    tcomp_lz77_tokens_free(&t);
    CHECK(tcomp_lz77_parse(s, 13, TCOMP_LZ77_WINDOW, 2, &t) == TCOMP_OK && t.count == 8);
    if (t.count == 8) {
        CHECK(t.items[7].length == 4 && t.items[7].distance == 9);
    }
    tcomp_lz77_tokens_free(&t);
}

TEST(test_short_chain_still_correct) {
    /* A chain limit of 1 finds worse matches but must never find wrong ones. */
    uint8_t data[20000];
    uint32_t rng = 4242;
    fill_wordy(data, sizeof data, &rng);
    CHECK(roundtrips(data, sizeof data, TCOMP_LZ77_WINDOW, 1));
    CHECK(roundtrips(data, sizeof data, TCOMP_LZ77_WINDOW, 2));
}

/* ---- expansion ---------------------------------------------------------- */

TEST(test_expand_overlap_and_limits) {
    const tcomp_lz77_token run[] = {LIT('a'), LIT('b'), MATCH(7, 2)};
    uint8_t out[16];
    size_t n = 0;
    CHECK(tcomp_lz77_expand(run, 3, out, sizeof out, &n) == TCOMP_OK);
    CHECK(n == 9 && memcmp(out, "ababababa", 9) == 0);
    /* Exactly enough room is fine; one byte less is not. */
    CHECK(tcomp_lz77_expand(run, 3, out, 9, &n) == TCOMP_OK);
    CHECK(tcomp_lz77_expand(run, 3, out, 8, &n) == TCOMP_ERR_CORRUPT);
}

TEST(test_expand_rejects_invalid_tokens) {
    uint8_t out[600];
    size_t n;
    const tcomp_lz77_token before_start[] = {LIT('a'), MATCH(3, 2)};
    CHECK(tcomp_lz77_expand(before_start, 2, out, sizeof out, &n) == TCOMP_ERR_CORRUPT);
    const tcomp_lz77_token too_short[] = {LIT('a'), MATCH(2, 1)};
    CHECK(tcomp_lz77_expand(too_short, 2, out, sizeof out, &n) == TCOMP_ERR_CORRUPT);
    const tcomp_lz77_token too_long[] = {LIT('a'), MATCH(259, 1)};
    CHECK(tcomp_lz77_expand(too_long, 2, out, sizeof out, &n) == TCOMP_ERR_CORRUPT);
    const tcomp_lz77_token bad_literal[] = {LIT(256)};
    CHECK(tcomp_lz77_expand(bad_literal, 1, out, sizeof out, &n) == TCOMP_ERR_CORRUPT);
    CHECK(tcomp_lz77_expand(bad_literal, 1, out, sizeof out, NULL) == TCOMP_ERR_INVALID_ARG);
}

/* ---- random roundtrips -------------------------------------------------- */

TEST(test_random_roundtrips) {
    enum { MAX_LEN = 20000 };
    uint8_t *data = malloc(MAX_LEN);
    CHECK(data != NULL);
    if (data == NULL) {
        return;
    }
    const size_t windows[] = {1, 7, 64, 4096, TCOMP_LZ77_WINDOW};
    const long chains[] = {NAIVE, 1, 4, TCOMP_LZ77_DEFAULT_CHAIN, TCOMP_LZ77_UNLIMITED_CHAIN};
    int failures = 0;
    for (uint32_t seed = 1; seed <= 8; seed++) {
        uint32_t rng = seed * 2654435761u;
        size_t len = next_random(&rng) % MAX_LEN;
        fill_wordy(data, len, &rng);
        for (size_t w = 0; w < sizeof windows / sizeof *windows; w++) {
            for (size_t c = 0; c < sizeof chains / sizeof *chains; c++) {
                failures += !roundtrips(data, len, windows[w], chains[c]);
            }
        }
    }
    CHECK(failures == 0);
    free(data);
}

void suite_lz77(void) {
    RUN(test_parse_repeat);
    RUN(test_parse_overlapping_run);
    RUN(test_parse_short_repeats_stay_literal);
    RUN(test_parse_prefers_nearest_on_ties);
    RUN(test_parse_caps_match_length);
    RUN(test_parse_respects_window);
    RUN(test_parse_rejects_bad_arguments);

    RUN(test_hash_chains_match_naive_on_random_inputs);
    RUN(test_hash_chains_match_naive_after_ring_wraps);
    RUN(test_chain_limit_counts_candidates);
    RUN(test_short_chain_still_correct);

    RUN(test_expand_overlap_and_limits);
    RUN(test_expand_rejects_invalid_tokens);

    RUN(test_random_roundtrips);
}
