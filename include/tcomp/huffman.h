/**
 * @file huffman.h
 * @brief Length-limited canonical Huffman coding.
 *
 * Huffman coding gives frequent symbols short bit codes and rare symbols long
 * ones. This module works in three steps, each usable on its own:
 *
 *   1. tcomp_huff_build_lengths(): symbol frequencies -> code length per symbol.
 *   2. tcomp_huff_assign_codes():  code lengths -> canonical codes.
 *   3. tcomp_huff_decoder_*():     code lengths -> a lookup table for decoding.
 *
 * Only the code lengths are ever stored in a file. Canonical codes can be
 * rebuilt from the lengths alone, so encoder and decoder agree on every code
 * without the tree being transmitted. See docs/decisions/0004-huffman-design.md.
 *
 * The module is alphabet-agnostic: M2 codes bytes (256 symbols), and M4 will
 * reuse it for LZ77 literal/length and distance alphabets.
 */
#ifndef TCOMP_HUFFMAN_H
#define TCOMP_HUFFMAN_H

#include <stddef.h>
#include <stdint.h>

#include "tcomp/bitio.h"
#include "tcomp/status.h"

/** Longest code length this module produces or accepts (same limit as DEFLATE). */
#define TCOMP_HUFF_MAX_BITS 15u

/** Largest alphabet size supported. */
#define TCOMP_HUFF_MAX_SYMBOLS 1024u

/**
 * @brief Compute optimal code lengths, limited to @p max_bits.
 *
 * Symbols with frequency 0 get length 0 (no code). If exactly one symbol is
 * used it gets length 1, since a code needs at least one bit. If the
 * unlimited Huffman tree would be deeper than @p max_bits, frequencies are
 * repeatedly halved (rounding up, so no used symbol drops to 0) and the tree
 * rebuilt; this flattens it until it fits, at a small cost in size.
 *
 * Ties are broken by symbol order, so the same input always gives the same
 * lengths on every platform.
 *
 * @param freqs    Frequency of each symbol. Length @p nsyms.
 * @param nsyms    Alphabet size, 1 to TCOMP_HUFF_MAX_SYMBOLS.
 * @param max_bits Length limit, 1 to TCOMP_HUFF_MAX_BITS. Must be large enough
 *                 for every used symbol: 2^max_bits >= number of used symbols.
 * @param lengths  Receives one code length per symbol. Length @p nsyms.
 * @return TCOMP_OK, TCOMP_ERR_INVALID_ARG or TCOMP_ERR_NOMEM.
 */
tcomp_status tcomp_huff_build_lengths(const uint64_t *freqs, size_t nsyms, unsigned max_bits,
                                      uint8_t *lengths);

/**
 * @brief Assign canonical codes from code lengths, validating them.
 *
 * Canonical rule: shorter codes come first; among codes of the same length,
 * smaller symbol values get smaller codes; each code is the previous one plus
 * one, shifted left when the length grows (RFC 1951, section 3.2.2).
 *
 * Lengths may describe an incomplete code (some bit patterns unused) but not
 * an oversubscribed one (more codes than bit patterns), which is impossible
 * and can only come from a corrupt file.
 *
 * @param lengths Code length per symbol, 0 (unused) to TCOMP_HUFF_MAX_BITS.
 * @param nsyms   Alphabet size, 1 to TCOMP_HUFF_MAX_SYMBOLS.
 * @param codes   Receives the code per symbol (meaningless where length is 0).
 * @return TCOMP_OK; TCOMP_ERR_INVALID_ARG for bad arguments;
 *         TCOMP_ERR_CORRUPT if a length exceeds the limit or the lengths are
 *         oversubscribed.
 */
tcomp_status tcomp_huff_assign_codes(const uint8_t *lengths, size_t nsyms, uint16_t *codes);

/**
 * @brief Table-driven decoder.
 *
 * The table has 2^bits entries, where bits is the longest code length. To
 * decode, peek `bits` bits, look up the entry, and consume only as many bits
 * as that symbol's code really has. Every entry whose index starts with a
 * symbol's code holds that symbol, so one lookup decodes any symbol.
 */
typedef struct {
    uint16_t *table; /**< Entry = symbol << 4 | code length; 0 = no code here. Owned. */
    unsigned bits;   /**< Longest code length; the table has 2^bits entries. */
} tcomp_huff_decoder;

/**
 * @brief Build a decoder from code lengths (validated as in tcomp_huff_assign_codes()).
 *
 * All-zero lengths are allowed and give a decoder that rejects every input.
 *
 * @return TCOMP_OK, TCOMP_ERR_INVALID_ARG, TCOMP_ERR_CORRUPT or TCOMP_ERR_NOMEM.
 *         On failure the decoder is still safe to pass to tcomp_huff_decoder_free().
 */
tcomp_status tcomp_huff_decoder_init(tcomp_huff_decoder *dec, const uint8_t *lengths, size_t nsyms);

/** @brief Release the decoder's table. Safe to call twice, or after a failed init. */
void tcomp_huff_decoder_free(tcomp_huff_decoder *dec);

/**
 * @brief Decode one symbol from @p br.
 *
 * @return TCOMP_OK; TCOMP_ERR_CORRUPT if the next bits match no code;
 *         TCOMP_ERR_TRUNCATED if the input ends inside a code. On error the
 *         reader does not move and @p sym is untouched.
 */
tcomp_status tcomp_huff_decode_symbol(const tcomp_huff_decoder *dec, tcomp_bitreader *br,
                                      unsigned *sym);

#endif /* TCOMP_HUFFMAN_H */
