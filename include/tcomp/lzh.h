/**
 * @file lzh.h
 * @brief LZH: LZ77 tokens coded with Huffman codes, DEFLATE-style.
 *
 * LZ77 (M3) removes repeated strings; Huffman (M2) gives frequent symbols
 * short codes. LZH does both, with the structure DEFLATE (RFC 1951) uses:
 *
 *   - A literal/length alphabet of 286 symbols: 0-255 are literal bytes,
 *     256 is reserved, 257-285 stand for match lengths 3-258.
 *   - A distance alphabet of 30 symbols for distances 1-32768.
 *   - Length and distance symbols cover ranges; "extra bits" after the
 *     symbol pick the exact value (length 13 = symbol 266 + 1 extra bit 0).
 *   - Each alphabet gets its own canonical Huffman code. Their code lengths
 *     are run-length coded and Huffman-coded with a small third code, so the
 *     tables cost tens of bytes rather than hundreds.
 *
 * The layout matches RFC 1951's dynamic-Huffman block but tcomp's bitstream
 * is MSB-first (decision 0003), so files are not interchangeable with gzip.
 * The exact format is in docs/FORMAT.md; the reasoning in
 * docs/decisions/0007-lzh-design.md.
 */
#ifndef TCOMP_LZH_H
#define TCOMP_LZH_H

#include <stddef.h>
#include <stdint.h>

#include "tcomp/bitio.h"
#include "tcomp/huffman.h"
#include "tcomp/lz77.h"
#include "tcomp/status.h"

#define TCOMP_LZH_LITLEN_SYMBOLS      286u /**< 256 literals, 1 reserved, 29 length codes. */
#define TCOMP_LZH_DIST_SYMBOLS        30u  /**< Distance codes. */
#define TCOMP_LZH_CODELEN_SYMBOLS     19u  /**< Code-length alphabet: 0-15, 16, 17, 18. */
#define TCOMP_LZH_FIRST_LENGTH_SYMBOL 257u
#define TCOMP_LZH_CODELEN_MAX_BITS    7u /**< Longest code in the code-length code. */

/**
 * @brief Symbol and extra bits for a match length (3-258).
 * @param length Must be 3 to 258.
 */
void tcomp_lzh_length_code(unsigned length, unsigned *symbol, unsigned *extra_bits,
                           unsigned *extra_value);

/**
 * @brief Symbol and extra bits for a match distance (1-32768).
 * @param distance Must be 1 to 32768.
 */
void tcomp_lzh_distance_code(unsigned distance, unsigned *symbol, unsigned *extra_bits,
                             unsigned *extra_value);

/** Everything needed to write an LZH bitstream, computed before writing. */
typedef struct {
    uint8_t litlen_lengths[TCOMP_LZH_LITLEN_SYMBOLS];
    uint16_t litlen_codes[TCOMP_LZH_LITLEN_SYMBOLS];
    uint8_t dist_lengths[TCOMP_LZH_DIST_SYMBOLS];
    uint16_t dist_codes[TCOMP_LZH_DIST_SYMBOLS];
    uint8_t codelen_lengths[TCOMP_LZH_CODELEN_SYMBOLS];
    uint16_t codelen_codes[TCOMP_LZH_CODELEN_SYMBOLS];
    unsigned hlit;  /**< Literal/length code lengths sent: 257-286. */
    unsigned hdist; /**< Distance code lengths sent: 1-30. */
    unsigned hclen; /**< Code-length code lengths sent: 4-19. */
    /** The code lengths, run-length coded: symbols 0-18 and their extra bits. */
    uint8_t rle_symbols[TCOMP_LZH_LITLEN_SYMBOLS + TCOMP_LZH_DIST_SYMBOLS];
    uint8_t rle_extra[TCOMP_LZH_LITLEN_SYMBOLS + TCOMP_LZH_DIST_SYMBOLS];
    size_t rle_count;
    uint64_t bits; /**< Exact size of the bitstream tcomp_lzh_write() produces. */
} tcomp_lzh_encoder;

/**
 * @brief Build the Huffman codes for @p tokens and compute the exact output size.
 *
 * Nothing is written; enc->bits tells the caller what writing would cost,
 * so AUTO can compare methods without encoding twice.
 *
 * @return TCOMP_OK, TCOMP_ERR_INVALID_ARG or TCOMP_ERR_NOMEM.
 */
tcomp_status tcomp_lzh_plan(const tcomp_lz77_token *tokens, size_t count, tcomp_lzh_encoder *enc);

/**
 * @brief Write the header (code tables) and every token to @p bw.
 *
 * @p enc must come from tcomp_lzh_plan() on the same tokens. Does not flush.
 * @return TCOMP_OK, TCOMP_ERR_INVALID_ARG or TCOMP_ERR_NOMEM.
 */
tcomp_status tcomp_lzh_write(const tcomp_lzh_encoder *enc, const tcomp_lz77_token *tokens,
                             size_t count, tcomp_bitwriter *bw);

/** Decoding tables for one LZH bitstream. */
typedef struct {
    tcomp_huff_decoder litlen;
    tcomp_huff_decoder dist;
} tcomp_lzh_decoder;

/**
 * @brief Read and validate the code tables at the start of an LZH bitstream.
 *
 * Treats input as untrusted. On failure the decoder is still safe to free.
 *
 * @return TCOMP_OK; TCOMP_ERR_TRUNCATED if the input ends inside the header;
 *         TCOMP_ERR_CORRUPT for out-of-range counts, a repeat with nothing to
 *         repeat, runs past the end of the tables, or impossible code
 *         lengths; TCOMP_ERR_NOMEM; TCOMP_ERR_INVALID_ARG.
 */
tcomp_status tcomp_lzh_read_header(tcomp_bitreader *br, tcomp_lzh_decoder *dec);

/**
 * @brief Read one token: a literal (distance 0, length = byte) or a match.
 *
 * Checks that the symbols and extra bits are well formed. Whether a match
 * reaches back further than the output so far is the caller's check.
 *
 * @return TCOMP_OK, TCOMP_ERR_TRUNCATED, TCOMP_ERR_CORRUPT (unknown or
 *         reserved symbol) or TCOMP_ERR_INVALID_ARG.
 */
tcomp_status tcomp_lzh_read_token(const tcomp_lzh_decoder *dec, tcomp_bitreader *br,
                                  tcomp_lz77_token *token);

/** @brief Release the decoder's tables. Safe to call twice, or after a failed read. */
void tcomp_lzh_decoder_free(tcomp_lzh_decoder *dec);

#endif /* TCOMP_LZH_H */
