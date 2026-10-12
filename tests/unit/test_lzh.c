/* Unit tests for LZH, the DEFLATE-style LZ77 + Huffman codec (src/lzh.c).
 *
 *   1. Length and distance codes: RFC 1951 known answers, and every length
 *      and distance checked to map to a distinct (symbol, extra) pair.
 *   2. Run-length coding of the code tables: exact symbol sequences.
 *   3. Roundtrips: plan -> write -> read gives back the same tokens, and the
 *      planned size equals the bits actually written.
 *   4. Hostile headers built bit by bit: every check in the header reader.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tcomp/bitio.h"
#include "tcomp/lz77.h"
#include "tcomp/lzh.h"
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

/* Plan, write and read back `tokens`; 1 if every token survives and the
 * planned size matches the bits written exactly. */
static int roundtrips(const tcomp_lz77_token *tokens, size_t count) {
    tcomp_lzh_encoder enc;
    tcomp_bitwriter bw;
    if (tcomp_lzh_plan(tokens, count, &enc) != TCOMP_OK || tcomp_bw_init(&bw, 0) != TCOMP_OK) {
        return 0;
    }
    int ok = tcomp_lzh_write(&enc, tokens, count, &bw) == TCOMP_OK &&
             tcomp_bw_bit_count(&bw) == enc.bits && tcomp_bw_flush(&bw) == TCOMP_OK;

    tcomp_bitreader br;
    tcomp_lzh_decoder dec;
    ok = ok && tcomp_br_init(&br, tcomp_bw_data(&bw), tcomp_bw_size(&bw)) == TCOMP_OK;
    ok = ok && tcomp_lzh_read_header(&br, &dec) == TCOMP_OK;
    for (size_t i = 0; ok && i < count; i++) {
        tcomp_lz77_token t;
        ok = tcomp_lzh_read_token(&dec, &br, &t) == TCOMP_OK && t.length == tokens[i].length &&
             t.distance == tokens[i].distance;
    }
    ok = ok && tcomp_br_bits_remaining(&br) < 8;
    tcomp_lzh_decoder_free(&dec);
    tcomp_bw_free(&bw);
    return ok;
}

/* Read a header from hand-written bits; returns the status. */
static tcomp_status read_header_of(const tcomp_bitwriter *bw) {
    tcomp_bitreader br;
    tcomp_lzh_decoder dec;
    tcomp_status st = tcomp_br_init(&br, tcomp_bw_data(bw), tcomp_bw_size(bw));
    if (st == TCOMP_OK) {
        st = tcomp_lzh_read_header(&br, &dec);
        tcomp_lzh_decoder_free(&dec);
    }
    return st;
}

/* Write the counts and a code-length code where code-length symbol a has
 * code '0' and symbol b has code '1' (a < b), all others unused. HCLEN is
 * 19 so every code-length symbol's length is spelled out. */
static void write_counts_and_two_symbol_codelen(tcomp_bitwriter *bw, unsigned hlit, unsigned hdist,
                                                unsigned a, unsigned b) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                      11, 4,  12, 3, 13, 2, 14, 1, 15};
    tcomp_bw_write_bits(bw, hlit - 257, 5);
    tcomp_bw_write_bits(bw, hdist - 1, 5);
    tcomp_bw_write_bits(bw, 19 - 4, 4);
    for (int i = 0; i < 19; i++) {
        tcomp_bw_write_bits(bw, (order[i] == a || order[i] == b) ? 1 : 0, 3);
    }
}

/* ---- length and distance codes ------------------------------------------ */

TEST(test_length_codes_known_answers) {
    unsigned sym, nbits, value;
    struct {
        unsigned length, sym, nbits, value;
    } cases[] = {{3, 257, 0, 0},    {10, 264, 0, 0}, {11, 265, 1, 0},   {12, 265, 1, 1},
                 {13, 266, 1, 0},   {19, 269, 2, 0}, {130, 280, 4, 15}, {227, 284, 5, 0},
                 {257, 284, 5, 30}, {258, 285, 0, 0}};
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        tcomp_lzh_length_code(cases[i].length, &sym, &nbits, &value);
        CHECK(sym == cases[i].sym && nbits == cases[i].nbits && value == cases[i].value);
    }
}

TEST(test_distance_codes_known_answers) {
    unsigned sym, nbits, value;
    struct {
        unsigned distance, sym, nbits, value;
    } cases[] = {{1, 0, 0, 0}, {4, 3, 0, 0},          {5, 4, 1, 0},       {6, 4, 1, 1},
                 {7, 5, 1, 0}, {24576, 28, 13, 8191}, {24577, 29, 13, 0}, {32768, 29, 13, 8191}};
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        tcomp_lzh_distance_code(cases[i].distance, &sym, &nbits, &value);
        CHECK(sym == cases[i].sym && nbits == cases[i].nbits && value == cases[i].value);
    }
}

TEST(test_codes_are_one_to_one) {
    /* Walking lengths upwards, (symbol, extra) must strictly increase and the
     * extra value must fit its bits: every length has exactly one encoding. */
    unsigned prev_sym = 0, prev_val = 0, bad = 0;
    for (unsigned len = 3; len <= 258; len++) {
        unsigned sym, nbits, value;
        tcomp_lzh_length_code(len, &sym, &nbits, &value);
        bad += sym < 257 || sym > 285 || value >= (1u << nbits);
        bad += len > 3 && !(sym > prev_sym || (sym == prev_sym && value == prev_val + 1));
        prev_sym = sym;
        prev_val = value;
    }
    for (unsigned d = 1; d <= 32768; d++) {
        unsigned sym, nbits, value;
        tcomp_lzh_distance_code(d, &sym, &nbits, &value);
        bad += sym > 29 || value >= (1u << nbits);
        bad += d > 1 && !(sym > prev_sym || (sym == prev_sym && value == prev_val + 1));
        prev_sym = sym;
        prev_val = value;
    }
    CHECK(bad == 0);
}

/* ---- run-length coded tables -------------------------------------------- */

TEST(test_rle_single_literal_symbol) {
    /* Only 'a' (97) is used: lengths are 97 zeros, a 1, then 159 zeros up to
     * HLIT = 257 and one zero for HDIST = 1, so 160 zeros in a row. */
    const tcomp_lz77_token tokens[] = {{'a', 0}, {'a', 0}};
    tcomp_lzh_encoder enc;
    CHECK(tcomp_lzh_plan(tokens, 2, &enc) == TCOMP_OK);
    CHECK(enc.hlit == 257 && enc.hdist == 1);
    const uint8_t sym[] = {18, 1, 18, 18};
    const uint8_t extra[] = {97 - 11, 0, 138 - 11, 22 - 11};
    CHECK(enc.rle_count == 4);
    if (enc.rle_count == 4) {
        CHECK(memcmp(enc.rle_symbols, sym, 4) == 0 && memcmp(enc.rle_extra, extra, 4) == 0);
    }
}

TEST(test_rle_repeats_nonzero_lengths) {
    /* 'a'..'h' equally often: eight 3-bit codes in a row. Sent as one 3,
     * then 16 (repeat 6 times), then a last 3. */
    tcomp_lz77_token tokens[8];
    for (int i = 0; i < 8; i++) {
        tokens[i].length = (uint16_t)('a' + i);
        tokens[i].distance = 0;
    }
    tcomp_lzh_encoder enc;
    CHECK(tcomp_lzh_plan(tokens, 8, &enc) == TCOMP_OK);
    const uint8_t sym[] = {18, 3, 16, 3, 18, 18};
    const uint8_t extra[] = {97 - 11, 0, 6 - 3, 0, 138 - 11, 15 - 11};
    CHECK(enc.rle_count == 6);
    if (enc.rle_count == 6) {
        CHECK(memcmp(enc.rle_symbols, sym, 6) == 0 && memcmp(enc.rle_extra, extra, 6) == 0);
    }
}

TEST(test_rle_short_zero_run) {
    /* 'a' (97) and 'e' (101): between their 1-bit codes are 3 unused
     * symbols, a zero run short enough for symbol 17 (3-10 zeros). */
    const tcomp_lz77_token tokens[] = {{'a', 0}, {'e', 0}};
    tcomp_lzh_encoder enc;
    CHECK(tcomp_lzh_plan(tokens, 2, &enc) == TCOMP_OK);
    const uint8_t sym[] = {18, 1, 17, 1, 18, 18};
    const uint8_t extra[] = {97 - 11, 0, 3 - 3, 0, 138 - 11, 18 - 11};
    CHECK(enc.rle_count == 6);
    if (enc.rle_count == 6) {
        CHECK(memcmp(enc.rle_symbols, sym, 6) == 0 && memcmp(enc.rle_extra, extra, 6) == 0);
    }
}

/* ---- roundtrips --------------------------------------------------------- */

TEST(test_roundtrip_edge_cases) {
    CHECK(roundtrips(NULL, 0));
    const tcomp_lz77_token one[] = {{'x', 0}};
    CHECK(roundtrips(one, 1));
    const tcomp_lz77_token extremes[] = {{'a', 0}, {3, 1},   {258, 1}, {'b', 0},
                                         {257, 4}, {227, 2}, {255, 0}, {0, 0}};
    CHECK(roundtrips(extremes, 8));
}

TEST(test_roundtrip_random_tokens) {
    /* Every length and distance range, skewed so codes have varied lengths. */
    enum { COUNT = 20000 };
    tcomp_lz77_token *tokens = malloc(COUNT * sizeof *tokens);
    CHECK(tokens != NULL);
    if (tokens == NULL) {
        return;
    }
    int failures = 0;
    for (uint32_t seed = 1; seed <= 6; seed++) {
        uint32_t rng = seed * 2654435761u;
        for (size_t i = 0; i < COUNT; i++) {
            uint32_t r = next_random(&rng);
            if (r % 3 == 0) {
                tokens[i].length = (uint16_t)(next_random(&rng) % (seed * 40 + 1));
                tokens[i].distance = 0;
            } else {
                tokens[i].length = (uint16_t)(3 + next_random(&rng) % (r % 7 == 0 ? 256 : 20));
                uint32_t far = next_random(&rng) % 32768 + 1;
                tokens[i].distance = (uint16_t)(r % 2 ? far : far % 100 + 1);
            }
        }
        failures += !roundtrips(tokens, COUNT);
    }
    CHECK(failures == 0);
    free(tokens);
}

TEST(test_roundtrip_parsed_text) {
    /* Real tokens from the LZ77 parser on text-like input. */
    char text[30000];
    uint32_t rng = 31337;
    static const char *words[] = {"the ", "of ", "compression ", "and ", "huffman ", "\n"};
    size_t n = 0;
    while (n + 20 < sizeof text) {
        const char *w = words[next_random(&rng) % 6];
        memcpy(text + n, w, strlen(w));
        n += strlen(w);
    }
    tcomp_lz77_tokens t;
    tcomp_lz77_tokens_init(&t);
    CHECK(tcomp_lz77_parse((const uint8_t *)text, n, TCOMP_LZ77_WINDOW, TCOMP_LZ77_DEFAULT_CHAIN,
                           &t) == TCOMP_OK);
    CHECK(roundtrips(t.items, t.count));
    tcomp_lz77_tokens_free(&t);
}

/* ---- hostile headers ---------------------------------------------------- */

TEST(test_header_rejects_bad_counts) {
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    tcomp_bw_write_bits(&bw, 30, 5); /* HLIT 287 > 286 */
    tcomp_bw_write_bits(&bw, 0, 5);
    tcomp_bw_write_bits(&bw, 0, 4);
    tcomp_bw_flush(&bw);
    CHECK(read_header_of(&bw) == TCOMP_ERR_CORRUPT);
    tcomp_bw_free(&bw);

    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    tcomp_bw_write_bits(&bw, 0, 5);
    tcomp_bw_write_bits(&bw, 31, 5); /* HDIST 32 > 30 */
    tcomp_bw_write_bits(&bw, 0, 4);
    tcomp_bw_flush(&bw);
    CHECK(read_header_of(&bw) == TCOMP_ERR_CORRUPT);
    tcomp_bw_free(&bw);

    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    tcomp_bw_write_bits(&bw, 0, 5); /* ends before HCLEN */
    tcomp_bw_flush(&bw);
    CHECK(read_header_of(&bw) == TCOMP_ERR_TRUNCATED);
    tcomp_bw_free(&bw);
}

TEST(test_header_rejects_repeat_at_start) {
    /* Code-length symbols 0 -> '0' and 16 -> '1'; the first symbol is 16,
     * "repeat the previous length", but there is no previous length. */
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    write_counts_and_two_symbol_codelen(&bw, 257, 1, 0, 16);
    tcomp_bw_write_bits(&bw, 1, 1); /* 16 */
    tcomp_bw_write_bits(&bw, 0, 2);
    tcomp_bw_flush(&bw);
    CHECK(read_header_of(&bw) == TCOMP_ERR_CORRUPT);
    tcomp_bw_free(&bw);
}

TEST(test_header_rejects_run_past_end) {
    /* 258 lengths expected; two runs of 138 zeros overshoot. */
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    write_counts_and_two_symbol_codelen(&bw, 257, 1, 0, 18);
    for (int i = 0; i < 2; i++) {
        tcomp_bw_write_bits(&bw, 1, 1); /* 18 */
        tcomp_bw_write_bits(&bw, 127, 7);
    }
    tcomp_bw_flush(&bw);
    CHECK(read_header_of(&bw) == TCOMP_ERR_CORRUPT);
    tcomp_bw_free(&bw);
}

TEST(test_header_rejects_impossible_codelen_code) {
    /* Four code-length symbols each claiming a 1-bit code: oversubscribed. */
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    tcomp_bw_write_bits(&bw, 0, 5);
    tcomp_bw_write_bits(&bw, 0, 5);
    tcomp_bw_write_bits(&bw, 0, 4); /* HCLEN 4: symbols 16, 17, 18, 0 */
    for (int i = 0; i < 4; i++) {
        tcomp_bw_write_bits(&bw, 1, 3);
    }
    tcomp_bw_write_bits(&bw, 0, 8);
    tcomp_bw_flush(&bw);
    CHECK(read_header_of(&bw) == TCOMP_ERR_CORRUPT);
    tcomp_bw_free(&bw);
}

TEST(test_token_rejects_reserved_symbol) {
    /* Hand-built tables: literal 'a' and the reserved symbol 256 each get a
     * 1-bit code ('a' -> '0', 256 -> '1'), via code-length symbols 0 and 1. */
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    write_counts_and_two_symbol_codelen(&bw, 257, 1, 0, 1);
    for (unsigned s = 0; s < 258; s++) { /* code-length '0' = 0, '1' = 1 */
        tcomp_bw_write_bits(&bw, (s == 'a' || s == 256) ? 1 : 0, 1);
    }
    tcomp_bw_write_bits(&bw, 0, 1); /* token 'a' */
    tcomp_bw_write_bits(&bw, 1, 1); /* token 256 */
    tcomp_bw_flush(&bw);

    tcomp_bitreader br;
    tcomp_lzh_decoder dec;
    tcomp_lz77_token t;
    CHECK(tcomp_br_init(&br, tcomp_bw_data(&bw), tcomp_bw_size(&bw)) == TCOMP_OK);
    CHECK(tcomp_lzh_read_header(&br, &dec) == TCOMP_OK);
    CHECK(tcomp_lzh_read_token(&dec, &br, &t) == TCOMP_OK && t.length == 'a' && t.distance == 0);
    CHECK(tcomp_lzh_read_token(&dec, &br, &t) == TCOMP_ERR_CORRUPT);
    tcomp_lzh_decoder_free(&dec);
    tcomp_lzh_decoder_free(&dec); /* twice must be safe */
    tcomp_bw_free(&bw);
}

TEST(test_token_rejects_match_without_distance_code) {
    /* Hand-built tables the encoder would never produce: the literal/length
     * code has 'a' -> '0' and length symbol 257 -> '1', but the distance
     * table is empty. A match must be rejected, not decoded with garbage. */
    tcomp_bitwriter bw;
    CHECK(tcomp_bw_init(&bw, 0) == TCOMP_OK);
    write_counts_and_two_symbol_codelen(&bw, 258, 1, 0, 1);
    for (unsigned s = 0; s < 259; s++) { /* 258 lit/len lengths + 1 distance length */
        tcomp_bw_write_bits(&bw, (s == 'a' || s == 257) ? 1 : 0, 1);
    }
    tcomp_bw_write_bits(&bw, 1, 1); /* symbol 257: a match of length 3 */
    tcomp_bw_write_bits(&bw, 0, 7);
    tcomp_bw_flush(&bw);

    tcomp_bitreader br;
    tcomp_lzh_decoder dec;
    tcomp_lz77_token t;
    CHECK(tcomp_br_init(&br, tcomp_bw_data(&bw), tcomp_bw_size(&bw)) == TCOMP_OK);
    CHECK(tcomp_lzh_read_header(&br, &dec) == TCOMP_OK);
    CHECK(tcomp_lzh_read_token(&dec, &br, &t) == TCOMP_ERR_CORRUPT);
    tcomp_lzh_decoder_free(&dec);
    tcomp_bw_free(&bw);
}

void suite_lzh(void) {
    RUN(test_length_codes_known_answers);
    RUN(test_distance_codes_known_answers);
    RUN(test_codes_are_one_to_one);

    RUN(test_rle_single_literal_symbol);
    RUN(test_rle_repeats_nonzero_lengths);
    RUN(test_rle_short_zero_run);

    RUN(test_roundtrip_edge_cases);
    RUN(test_roundtrip_random_tokens);
    RUN(test_roundtrip_parsed_text);

    RUN(test_header_rejects_bad_counts);
    RUN(test_header_rejects_repeat_at_start);
    RUN(test_header_rejects_run_past_end);
    RUN(test_header_rejects_impossible_codelen_code);
    RUN(test_token_rejects_reserved_symbol);
    RUN(test_token_rejects_match_without_distance_code);
}
