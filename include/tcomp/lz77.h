/**
 * @file lz77.h
 * @brief LZ77: replace repeated byte strings with back-references.
 *
 * The input becomes a sequence of tokens. A token is either a literal byte,
 * or a match "copy `length` bytes starting `distance` bytes back in the
 * output so far". The parameters are DEFLATE's: matches are 3 to 258 bytes
 * long and reach back at most 32 KB (the window).
 *
 *   "abcabcabcx"  ->  'a' 'b' 'c' (length 6, distance 3) 'x'
 *
 * A match may overlap the bytes it produces (distance < length): the copy
 * proceeds byte by byte, so a short pattern repeats, like run-length coding.
 *
 * This module only turns bytes into tokens and back. How tokens are packed
 * into bits is the container's job: fixed-width fields in M3-M4, Huffman codes
 * in M5. See docs/decisions/0005-lz77-design.md.
 */
#ifndef TCOMP_LZ77_H
#define TCOMP_LZ77_H

#include <stddef.h>
#include <stdint.h>

#include "tcomp/status.h"

#define TCOMP_LZ77_MIN_MATCH 3u     /**< Shorter matches cost more bits than literals. */
#define TCOMP_LZ77_MAX_MATCH 258u   /**< Longest match (DEFLATE's limit). */
#define TCOMP_LZ77_WINDOW    32768u /**< Furthest a match can reach back, in bytes. */

/** One literal or one match. */
typedef struct {
    uint16_t length;   /**< Match: 3 to 258. Literal: the byte value, 0 to 255. */
    uint16_t distance; /**< Match: 1 to 32768. Literal: 0. */
} tcomp_lz77_token;

/** A growable token array. Owns its memory. */
typedef struct {
    tcomp_lz77_token *items;
    size_t count;
    size_t capacity;
} tcomp_lz77_tokens;

/** @brief Prepare an empty token array. Does not allocate. */
void tcomp_lz77_tokens_init(tcomp_lz77_tokens *tokens);

/** @brief Release the array. Safe to call twice. */
void tcomp_lz77_tokens_free(tcomp_lz77_tokens *tokens);

/** Pass as max_chain to tcomp_lz77_parse() to search every candidate. */
#define TCOMP_LZ77_UNLIMITED_CHAIN 0u

/**
 * Default max_chain: candidates tried per position. Chosen from the
 * ratio/speed sweep in docs/BENCHMARKS.md (M4).
 */
#define TCOMP_LZ77_DEFAULT_CHAIN 128u

/**
 * @brief Split @p data into tokens: greedy parsing, hash-chain matcher.
 *
 * Every position is indexed by a hash of the 3 bytes starting there. To find
 * a match, only earlier positions with the same hash are tried, nearest
 * first, following a chain of links; any match of 3 or more bytes must start
 * with the same 3 bytes, so no useful candidate is skipped. The longest
 * match wins (the nearest on ties); if it is at least 3 bytes it is emitted,
 * otherwise one literal.
 *
 * With @p max_chain = TCOMP_LZ77_UNLIMITED_CHAIN the tokens are identical to
 * tcomp_lz77_parse_naive(). A limit trades a little compression for a
 * bounded amount of work per position.
 *
 * @param data      Input bytes. May be NULL only if @p len is 0.
 * @param len       Input length.
 * @param window    How far back to search, 1 to TCOMP_LZ77_WINDOW.
 * @param max_chain Candidates tried per position at most, or
 *                  TCOMP_LZ77_UNLIMITED_CHAIN.
 * @param tokens    Initialised array; tokens are appended to it.
 * @return TCOMP_OK, TCOMP_ERR_INVALID_ARG or TCOMP_ERR_NOMEM.
 */
tcomp_status tcomp_lz77_parse(const uint8_t *data, size_t len, size_t window, size_t max_chain,
                              tcomp_lz77_tokens *tokens);

/**
 * @brief Reference parser: greedy parsing with a naive matcher.
 *
 * At each position, every earlier position within the window is tried. Same
 * rules and same output as tcomp_lz77_parse() with an unlimited chain, but
 * O(n x window) time (0.2 MB/s on text, 0.04 MB/s on random data). Kept as
 * the reference that the hash-chain matcher is tested and benchmarked
 * against; not used for compression.
 *
 * Parameters and return values as tcomp_lz77_parse(), without max_chain.
 */
tcomp_status tcomp_lz77_parse_naive(const uint8_t *data, size_t len, size_t window,
                                    tcomp_lz77_tokens *tokens);

/**
 * @brief Rebuild bytes from tokens into a caller-provided buffer.
 *
 * Validates every token, so it is safe on tokens decoded from a file.
 *
 * @param tokens  Tokens to expand.
 * @param count   Number of tokens.
 * @param out     Output buffer of @p cap bytes. May be NULL only if @p cap is 0.
 * @param cap     Output capacity.
 * @param out_len Receives the number of bytes written.
 * @return TCOMP_OK; TCOMP_ERR_INVALID_ARG for bad arguments; TCOMP_ERR_CORRUPT
 *         if a token is out of range, reaches back before the start of the
 *         output, or would overflow @p cap.
 */
tcomp_status tcomp_lz77_expand(const tcomp_lz77_token *tokens, size_t count, uint8_t *out,
                               size_t cap, size_t *out_len);

#endif /* TCOMP_LZ77_H */
