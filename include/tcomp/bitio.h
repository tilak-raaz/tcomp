/**
 * @file bitio.h
 * @brief Writing and reading individual bits and variable-width bit fields.
 *
 * Compression produces values that are not whole bytes: a Huffman code may be
 * 3 bits, the next 11 bits, the next 1 bit. This module packs such fields
 * into bytes and unpacks them again.
 *
 * Bit order is MSB-first: the first bit written becomes the highest bit
 * (0x80) of the first byte, and a multi-bit value is written from its most
 * significant bit down. Writing 1, then 0, then 1 and flushing gives the
 * single byte 0b10100000 (0xA0). See docs/decisions/0003-bit-order.md.
 *
 * The writer appends to a growable buffer it owns. The reader reads from a
 * buffer the caller owns and never modifies it. Both operate on memory, not
 * FILE streams, so a block can be encoded in memory and written later (M7)
 * or by another thread (M9).
 */
#ifndef TCOMP_BITIO_H
#define TCOMP_BITIO_H

#include <stddef.h>
#include <stdint.h>

#include "tcomp/status.h"

/** Largest field width, in bits, accepted by one write/read/peek call. */
#define TCOMP_BITIO_MAX_BITS 32u

/* ---- writer ------------------------------------------------------------- */

/**
 * @brief Packs bit fields into a growable byte buffer.
 *
 * Treat the fields as private; use the functions below. Zero-initialising
 * with `= {0}` is not enough: call tcomp_bw_init().
 */
typedef struct {
    uint8_t *data;     /**< Completed bytes. Owned by the writer. */
    size_t size;       /**< Number of completed bytes in data. */
    size_t capacity;   /**< Allocated length of data. */
    uint64_t acc;      /**< Pending bits, right-aligned (low acc_bits bits are valid). */
    unsigned acc_bits; /**< Number of pending bits in acc, always < 8 between calls. */
} tcomp_bitwriter;

/**
 * @brief Prepare an empty writer.
 *
 * @param bw               Writer to initialise. Must not be NULL.
 * @param initial_capacity Bytes to allocate up front; 0 picks a default.
 *                         The buffer grows automatically either way.
 * @return TCOMP_OK, TCOMP_ERR_INVALID_ARG or TCOMP_ERR_NOMEM. On failure the
 *         writer is still safe to pass to tcomp_bw_free().
 */
tcomp_status tcomp_bw_init(tcomp_bitwriter *bw, size_t initial_capacity);

/**
 * @brief Release the writer's buffer. Safe to call twice, or after a failed init.
 */
void tcomp_bw_free(tcomp_bitwriter *bw);

/**
 * @brief Append the low @p nbits bits of @p value, most significant first.
 *
 * @param bw    Initialised writer.
 * @param value Field to write. Must fit in @p nbits bits.
 * @param nbits Width, 0 to 32. Writing 0 bits does nothing.
 * @return TCOMP_OK; TCOMP_ERR_INVALID_ARG if nbits > 32 or value has bits set
 *         above nbits (both are caller bugs, reported rather than silently
 *         masked); TCOMP_ERR_NOMEM if the buffer could not grow. On error
 *         nothing is written.
 */
tcomp_status tcomp_bw_write_bits(tcomp_bitwriter *bw, uint32_t value, unsigned nbits);

/** @brief Append one bit. @p bit must be 0 or 1. Same errors as tcomp_bw_write_bits(). */
tcomp_status tcomp_bw_write_bit(tcomp_bitwriter *bw, unsigned bit);

/**
 * @brief Pad with 0 bits up to the next byte boundary.
 *
 * Call once at the end so the last partial byte is included in the output.
 * Writing may continue afterwards, starting on a fresh byte. Does nothing if
 * already aligned.
 *
 * @return TCOMP_OK or TCOMP_ERR_NOMEM.
 */
tcomp_status tcomp_bw_flush(tcomp_bitwriter *bw);

/** @brief Total bits written so far, including pending bits not yet flushed. */
size_t tcomp_bw_bit_count(const tcomp_bitwriter *bw);

/**
 * @brief The completed bytes. Pending bits are excluded until tcomp_bw_flush().
 *
 * The pointer stays valid until the next write, flush or free. Its length is
 * tcomp_bw_size(). It may be NULL when the size is 0.
 */
const uint8_t *tcomp_bw_data(const tcomp_bitwriter *bw);

/** @brief Number of completed bytes available from tcomp_bw_data(). */
size_t tcomp_bw_size(const tcomp_bitwriter *bw);

/* ---- reader ------------------------------------------------------------- */

/**
 * @brief Reads bit fields from a byte buffer. Never allocates.
 *
 * Every read is bounds-checked: input is assumed to be untrusted.
 */
typedef struct {
    const uint8_t *data; /**< Borrowed; must outlive the reader. */
    size_t size;         /**< Length of data in bytes. */
    size_t pos;          /**< Index of the next bit to read, 0 to size * 8. */
} tcomp_bitreader;

/**
 * @brief Start reading @p size bytes at @p data.
 *
 * @return TCOMP_OK, or TCOMP_ERR_INVALID_ARG if br is NULL, data is NULL with
 *         a non-zero size, or size * 8 would overflow size_t.
 */
tcomp_status tcomp_br_init(tcomp_bitreader *br, const uint8_t *data, size_t size);

/**
 * @brief Read @p nbits bits as an unsigned value, most significant first.
 *
 * @param out Receives the value; untouched on error.
 * @return TCOMP_OK; TCOMP_ERR_INVALID_ARG if nbits > 32 or out is NULL;
 *         TCOMP_ERR_TRUNCATED if fewer than nbits bits remain. On error the
 *         read position does not move.
 */
tcomp_status tcomp_br_read_bits(tcomp_bitreader *br, unsigned nbits, uint32_t *out);

/** @brief Read one bit into @p out (0 or 1). Same errors as tcomp_br_read_bits(). */
tcomp_status tcomp_br_read_bit(tcomp_bitreader *br, unsigned *out);

/**
 * @brief Look at the next @p nbits bits without consuming them.
 *
 * Bits past the end of the buffer read as 0, so a decoder can always peek a
 * full table index and then check how many bits it actually consumes with
 * tcomp_br_skip_bits(). Huffman table decoding (M2) relies on this.
 *
 * @param nbits Width, 0 to 32. Larger values are clamped to 32.
 */
uint32_t tcomp_br_peek_bits(const tcomp_bitreader *br, unsigned nbits);

/**
 * @brief Advance past @p nbits bits.
 *
 * @return TCOMP_OK, or TCOMP_ERR_TRUNCATED (position unchanged) if fewer than
 *         nbits bits remain.
 */
tcomp_status tcomp_br_skip_bits(tcomp_bitreader *br, size_t nbits);

/** @brief Skip to the start of the next byte. Does nothing if already aligned. */
void tcomp_br_align(tcomp_bitreader *br);

/** @brief Bits left to read. */
size_t tcomp_br_bits_remaining(const tcomp_bitreader *br);

#endif /* TCOMP_BITIO_H */
