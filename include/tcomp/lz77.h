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
 * into bits is the container's job: fixed-width fields in M3, Huffman codes
 * in M4. See docs/decisions/0005-lz77-design.md.
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

/**
 * @brief Split @p data into tokens with greedy parsing and a naive matcher.
 *
 * At each position, every earlier position within the window is tried and
 * the longest match wins (the nearest one on ties); if it is at least 3
 * bytes it is emitted and skipped over, otherwise one literal is emitted.
 * This costs O(n x window) time: simple and obviously correct, and the
 * baseline that the hash-chain matcher in M6 is measured against.
 *
 * @param data   Input bytes. May be NULL only if @p len is 0.
 * @param len    Input length.
 * @param window How far back to search, 1 to TCOMP_LZ77_WINDOW. Smaller
 *               windows are faster and are used by tests.
 * @param tokens Initialised array; tokens are appended to it.
 * @return TCOMP_OK, TCOMP_ERR_INVALID_ARG or TCOMP_ERR_NOMEM.
 */
tcomp_status tcomp_lz77_parse(const uint8_t *data, size_t len, size_t window,
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
