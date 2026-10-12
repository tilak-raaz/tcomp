#include "tcomp/lzh.h"

#include <string.h>

/* ---- length and distance codes (RFC 1951, 3.2.5) ------------------------ */

enum { LENGTH_CODES = 29, REPEAT_PREVIOUS = 16, REPEAT_ZERO_SHORT = 17, REPEAT_ZERO_LONG = 18 };

static const uint16_t LENGTH_BASE[LENGTH_CODES] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                                   15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                                   67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LENGTH_EXTRA[LENGTH_CODES] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DIST_BASE[TCOMP_LZH_DIST_SYMBOLS] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DIST_EXTRA[TCOMP_LZH_DIST_SYMBOLS] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                                           4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                                           9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* Order in which code-length code lengths are sent: likely-used first, so
 * trailing unused ones can be dropped (RFC 1951, 3.2.7). */
static const uint8_t CODELEN_ORDER[TCOMP_LZH_CODELEN_SYMBOLS] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                                 11, 4,  12, 3, 13, 2, 14, 1, 15};

/* Extra bits after each code-length symbol: 16 repeats 3-6 times (2 bits),
 * 17 writes 3-10 zeros (3 bits), 18 writes 11-138 zeros (7 bits). */
static unsigned codelen_extra_bits(unsigned symbol) {
    return symbol == REPEAT_PREVIOUS     ? 2
           : symbol == REPEAT_ZERO_SHORT ? 3
           : symbol == REPEAT_ZERO_LONG  ? 7
                                         : 0;
}

/* Index of the last entry in base[0..n) that is <= value (base is sorted). */
static unsigned floor_index(const uint16_t *base, unsigned n, unsigned value) {
    unsigned lo = 0, hi = n - 1;
    while (lo < hi) {
        unsigned mid = (lo + hi + 1) / 2;
        if (base[mid] <= value) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

void tcomp_lzh_length_code(unsigned length, unsigned *symbol, unsigned *extra_bits,
                           unsigned *extra_value) {
    unsigned i = floor_index(LENGTH_BASE, LENGTH_CODES, length);
    *symbol = TCOMP_LZH_FIRST_LENGTH_SYMBOL + i;
    *extra_bits = LENGTH_EXTRA[i];
    *extra_value = length - LENGTH_BASE[i];
}

void tcomp_lzh_distance_code(unsigned distance, unsigned *symbol, unsigned *extra_bits,
                             unsigned *extra_value) {
    unsigned i = floor_index(DIST_BASE, TCOMP_LZH_DIST_SYMBOLS, distance);
    *symbol = i;
    *extra_bits = DIST_EXTRA[i];
    *extra_value = distance - DIST_BASE[i];
}

/* ---- planning ----------------------------------------------------------- */

static void rle_emit(tcomp_lzh_encoder *enc, unsigned symbol, unsigned extra) {
    enc->rle_symbols[enc->rle_count] = (uint8_t)symbol;
    enc->rle_extra[enc->rle_count] = (uint8_t)extra;
    enc->rle_count++;
}

/* Run-length code the code lengths, as DEFLATE does: runs of zeros become
 * 17/18, other runs become the length once followed by 16s. */
static void rle_encode(tcomp_lzh_encoder *enc, const uint8_t *lengths, unsigned total) {
    enc->rle_count = 0;
    unsigned i = 0;
    while (i < total) {
        unsigned value = lengths[i], run = 1;
        while (i + run < total && lengths[i + run] == value) {
            run++;
        }
        i += run;
        if (value == 0) {
            while (run >= 11) {
                unsigned n = run < 138 ? run : 138;
                rle_emit(enc, REPEAT_ZERO_LONG, n - 11);
                run -= n;
            }
            if (run >= 3) {
                rle_emit(enc, REPEAT_ZERO_SHORT, run - 3);
                run = 0;
            }
        } else {
            rle_emit(enc, value, 0); /* 16 repeats the previous length */
            run--;
            while (run >= 3) {
                unsigned n = run < 6 ? run : 6;
                rle_emit(enc, REPEAT_PREVIOUS, n - 3);
                run -= n;
            }
        }
        while (run > 0) { /* short leftovers are cheaper sent as-is */
            rle_emit(enc, value, 0);
            run--;
        }
    }
}

tcomp_status tcomp_lzh_plan(const tcomp_lz77_token *tokens, size_t count, tcomp_lzh_encoder *enc) {
    if ((tokens == NULL && count > 0) || enc == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    memset(enc, 0, sizeof *enc);

    uint64_t litlen_freq[TCOMP_LZH_LITLEN_SYMBOLS] = {0};
    uint64_t dist_freq[TCOMP_LZH_DIST_SYMBOLS] = {0};
    uint64_t extra_bits = 0;
    for (size_t t = 0; t < count; t++) {
        if (tokens[t].distance == 0) {
            litlen_freq[tokens[t].length]++;
            continue;
        }
        unsigned sym, nbits, value;
        tcomp_lzh_length_code(tokens[t].length, &sym, &nbits, &value);
        litlen_freq[sym]++;
        extra_bits += nbits;
        tcomp_lzh_distance_code(tokens[t].distance, &sym, &nbits, &value);
        dist_freq[sym]++;
        extra_bits += nbits;
    }

    tcomp_status st = tcomp_huff_build_lengths(litlen_freq, TCOMP_LZH_LITLEN_SYMBOLS,
                                               TCOMP_HUFF_MAX_BITS, enc->litlen_lengths);
    if (st == TCOMP_OK) {
        st = tcomp_huff_build_lengths(dist_freq, TCOMP_LZH_DIST_SYMBOLS, TCOMP_HUFF_MAX_BITS,
                                      enc->dist_lengths);
    }
    if (st == TCOMP_OK) {
        st = tcomp_huff_assign_codes(enc->litlen_lengths, TCOMP_LZH_LITLEN_SYMBOLS,
                                     enc->litlen_codes);
    }
    if (st == TCOMP_OK) {
        st = tcomp_huff_assign_codes(enc->dist_lengths, TCOMP_LZH_DIST_SYMBOLS, enc->dist_codes);
    }
    if (st != TCOMP_OK) {
        return st;
    }

    /* Send code lengths only up to the last used symbol. */
    enc->hlit = TCOMP_LZH_LITLEN_SYMBOLS;
    while (enc->hlit > TCOMP_LZH_FIRST_LENGTH_SYMBOL && enc->litlen_lengths[enc->hlit - 1] == 0) {
        enc->hlit--;
    }
    enc->hdist = TCOMP_LZH_DIST_SYMBOLS;
    while (enc->hdist > 1 && enc->dist_lengths[enc->hdist - 1] == 0) {
        enc->hdist--;
    }

    /* Both tables form one sequence, so a run may cross from one to the other. */
    uint8_t all[TCOMP_LZH_LITLEN_SYMBOLS + TCOMP_LZH_DIST_SYMBOLS];
    memcpy(all, enc->litlen_lengths, enc->hlit);
    memcpy(all + enc->hlit, enc->dist_lengths, enc->hdist);
    rle_encode(enc, all, enc->hlit + enc->hdist);

    uint64_t codelen_freq[TCOMP_LZH_CODELEN_SYMBOLS] = {0};
    for (size_t i = 0; i < enc->rle_count; i++) {
        codelen_freq[enc->rle_symbols[i]]++;
    }
    st = tcomp_huff_build_lengths(codelen_freq, TCOMP_LZH_CODELEN_SYMBOLS,
                                  TCOMP_LZH_CODELEN_MAX_BITS, enc->codelen_lengths);
    if (st == TCOMP_OK) {
        st = tcomp_huff_assign_codes(enc->codelen_lengths, TCOMP_LZH_CODELEN_SYMBOLS,
                                     enc->codelen_codes);
    }
    if (st != TCOMP_OK) {
        return st;
    }
    enc->hclen = TCOMP_LZH_CODELEN_SYMBOLS;
    while (enc->hclen > 4 && enc->codelen_lengths[CODELEN_ORDER[enc->hclen - 1]] == 0) {
        enc->hclen--;
    }

    /* Exact size: counts, code-length code, RLE'd tables, then the tokens. */
    uint64_t bits = 5 + 5 + 4 + 3 * (uint64_t)enc->hclen;
    for (size_t i = 0; i < enc->rle_count; i++) {
        bits += enc->codelen_lengths[enc->rle_symbols[i]] + codelen_extra_bits(enc->rle_symbols[i]);
    }
    for (unsigned s = 0; s < TCOMP_LZH_LITLEN_SYMBOLS; s++) {
        bits += litlen_freq[s] * enc->litlen_lengths[s];
    }
    for (unsigned s = 0; s < TCOMP_LZH_DIST_SYMBOLS; s++) {
        bits += dist_freq[s] * enc->dist_lengths[s];
    }
    enc->bits = bits + extra_bits;
    return TCOMP_OK;
}

/* ---- writing ------------------------------------------------------------ */

tcomp_status tcomp_lzh_write(const tcomp_lzh_encoder *enc, const tcomp_lz77_token *tokens,
                             size_t count, tcomp_bitwriter *bw) {
    if (enc == NULL || bw == NULL || (tokens == NULL && count > 0)) {
        return TCOMP_ERR_INVALID_ARG;
    }
    tcomp_status st = tcomp_bw_write_bits(bw, enc->hlit - TCOMP_LZH_FIRST_LENGTH_SYMBOL, 5);
    if (st == TCOMP_OK) {
        st = tcomp_bw_write_bits(bw, enc->hdist - 1, 5);
    }
    if (st == TCOMP_OK) {
        st = tcomp_bw_write_bits(bw, enc->hclen - 4, 4);
    }
    for (unsigned i = 0; i < enc->hclen && st == TCOMP_OK; i++) {
        st = tcomp_bw_write_bits(bw, enc->codelen_lengths[CODELEN_ORDER[i]], 3);
    }
    for (size_t i = 0; i < enc->rle_count && st == TCOMP_OK; i++) {
        unsigned sym = enc->rle_symbols[i];
        st = tcomp_bw_write_bits(bw, enc->codelen_codes[sym], enc->codelen_lengths[sym]);
        if (st == TCOMP_OK) {
            st = tcomp_bw_write_bits(bw, enc->rle_extra[i], codelen_extra_bits(sym));
        }
    }

    for (size_t t = 0; t < count && st == TCOMP_OK; t++) {
        const tcomp_lz77_token *tok = &tokens[t];
        if (tok->distance == 0) {
            st = tcomp_bw_write_bits(bw, enc->litlen_codes[tok->length],
                                     enc->litlen_lengths[tok->length]);
            continue;
        }
        unsigned sym, nbits, value;
        tcomp_lzh_length_code(tok->length, &sym, &nbits, &value);
        st = tcomp_bw_write_bits(bw, enc->litlen_codes[sym], enc->litlen_lengths[sym]);
        if (st == TCOMP_OK) {
            st = tcomp_bw_write_bits(bw, value, nbits);
        }
        tcomp_lzh_distance_code(tok->distance, &sym, &nbits, &value);
        if (st == TCOMP_OK) {
            st = tcomp_bw_write_bits(bw, enc->dist_codes[sym], enc->dist_lengths[sym]);
        }
        if (st == TCOMP_OK) {
            st = tcomp_bw_write_bits(bw, value, nbits);
        }
    }
    return st;
}

/* ---- reading ------------------------------------------------------------ */

void tcomp_lzh_decoder_free(tcomp_lzh_decoder *dec) {
    if (dec == NULL) {
        return;
    }
    tcomp_huff_decoder_free(&dec->litlen);
    tcomp_huff_decoder_free(&dec->dist);
}

/* Read the run-length-coded code lengths of both tables into all[0..total). */
static tcomp_status read_code_lengths(tcomp_bitreader *br, const tcomp_huff_decoder *codelen,
                                      uint8_t *all, unsigned total) {
    unsigned i = 0;
    while (i < total) {
        unsigned sym;
        tcomp_status st = tcomp_huff_decode_symbol(codelen, br, &sym);
        if (st != TCOMP_OK) {
            return st;
        }
        if (sym < REPEAT_PREVIOUS) {
            all[i++] = (uint8_t)sym;
            continue;
        }
        uint32_t extra;
        st = tcomp_br_read_bits(br, codelen_extra_bits(sym), &extra);
        if (st != TCOMP_OK) {
            return st;
        }
        uint8_t value = 0;
        unsigned run;
        if (sym == REPEAT_PREVIOUS) {
            if (i == 0) {
                return TCOMP_ERR_CORRUPT; /* nothing to repeat */
            }
            value = all[i - 1];
            run = 3 + extra;
        } else {
            run = (sym == REPEAT_ZERO_SHORT ? 3 : 11) + extra;
        }
        if (run > total - i) {
            return TCOMP_ERR_CORRUPT; /* runs past the end of the tables */
        }
        memset(all + i, value, run);
        i += run;
    }
    return TCOMP_OK;
}

tcomp_status tcomp_lzh_read_header(tcomp_bitreader *br, tcomp_lzh_decoder *dec) {
    if (br == NULL || dec == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    memset(dec, 0, sizeof *dec);

    uint32_t hlit, hdist, hclen;
    tcomp_status st = tcomp_br_read_bits(br, 5, &hlit);
    if (st == TCOMP_OK) {
        st = tcomp_br_read_bits(br, 5, &hdist);
    }
    if (st == TCOMP_OK) {
        st = tcomp_br_read_bits(br, 4, &hclen);
    }
    if (st != TCOMP_OK) {
        return st;
    }
    hlit += TCOMP_LZH_FIRST_LENGTH_SYMBOL;
    hdist += 1;
    hclen += 4;
    if (hlit > TCOMP_LZH_LITLEN_SYMBOLS || hdist > TCOMP_LZH_DIST_SYMBOLS) {
        return TCOMP_ERR_CORRUPT; /* the 5-bit fields can say up to 288 and 32 */
    }

    uint8_t codelen_lengths[TCOMP_LZH_CODELEN_SYMBOLS] = {0};
    for (unsigned i = 0; i < hclen; i++) {
        uint32_t v;
        st = tcomp_br_read_bits(br, 3, &v);
        if (st != TCOMP_OK) {
            return st;
        }
        codelen_lengths[CODELEN_ORDER[i]] = (uint8_t)v;
    }

    tcomp_huff_decoder codelen;
    st = tcomp_huff_decoder_init(&codelen, codelen_lengths, TCOMP_LZH_CODELEN_SYMBOLS);
    uint8_t all[TCOMP_LZH_LITLEN_SYMBOLS + TCOMP_LZH_DIST_SYMBOLS];
    if (st == TCOMP_OK) {
        st = read_code_lengths(br, &codelen, all, hlit + hdist);
    }
    tcomp_huff_decoder_free(&codelen);

    if (st == TCOMP_OK) {
        st = tcomp_huff_decoder_init(&dec->litlen, all, hlit);
    }
    if (st == TCOMP_OK) {
        st = tcomp_huff_decoder_init(&dec->dist, all + hlit, hdist);
    }
    if (st != TCOMP_OK) {
        tcomp_lzh_decoder_free(dec);
    }
    return st;
}

tcomp_status tcomp_lzh_read_token(const tcomp_lzh_decoder *dec, tcomp_bitreader *br,
                                  tcomp_lz77_token *token) {
    if (dec == NULL || br == NULL || token == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    unsigned sym;
    tcomp_status st = tcomp_huff_decode_symbol(&dec->litlen, br, &sym);
    if (st != TCOMP_OK) {
        return st;
    }
    if (sym < 256) {
        token->length = (uint16_t)sym;
        token->distance = 0;
        return TCOMP_OK;
    }
    if (sym < TCOMP_LZH_FIRST_LENGTH_SYMBOL) {
        return TCOMP_ERR_CORRUPT; /* 256 is reserved */
    }

    unsigned i = sym - TCOMP_LZH_FIRST_LENGTH_SYMBOL;
    uint32_t extra;
    st = tcomp_br_read_bits(br, LENGTH_EXTRA[i], &extra);
    if (st != TCOMP_OK) {
        return st;
    }
    unsigned length = LENGTH_BASE[i] + extra;

    st = tcomp_huff_decode_symbol(&dec->dist, br, &sym);
    if (st != TCOMP_OK) {
        return st;
    }
    st = tcomp_br_read_bits(br, DIST_EXTRA[sym], &extra);
    if (st != TCOMP_OK) {
        return st;
    }
    /* No range check needed: the tables top out at exactly 258 (227 + 31)
     * and 32768 (24577 + 8191), so every symbol + extra bits is in range. */
    token->length = (uint16_t)length;
    token->distance = (uint16_t)(DIST_BASE[sym] + extra);
    return TCOMP_OK;
}
