#include "tcomp/bitio.h"

#include <stdlib.h>
#include <string.h>

enum { DEFAULT_CAPACITY = 256 };

/* ---- writer ------------------------------------------------------------- */

/* Make room for `extra` more bytes, doubling the buffer as needed so that
 * appending n bytes one call at a time costs O(n) overall. */
static tcomp_status reserve(tcomp_bitwriter *bw, size_t extra) {
    if (extra <= bw->capacity - bw->size) {
        return TCOMP_OK;
    }
    if (extra > SIZE_MAX - bw->size) {
        return TCOMP_ERR_NOMEM;
    }
    size_t needed = bw->size + extra;
    size_t cap = bw->capacity > 0 ? bw->capacity : DEFAULT_CAPACITY;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) {
            cap = needed;
            break;
        }
        cap *= 2;
    }
    uint8_t *grown = realloc(bw->data, cap);
    if (grown == NULL) {
        return TCOMP_ERR_NOMEM;
    }
    bw->data = grown;
    bw->capacity = cap;
    return TCOMP_OK;
}

tcomp_status tcomp_bw_init(tcomp_bitwriter *bw, size_t initial_capacity) {
    if (bw == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    memset(bw, 0, sizeof *bw);
    size_t cap = initial_capacity > 0 ? initial_capacity : DEFAULT_CAPACITY;
    bw->data = malloc(cap);
    if (bw->data == NULL) {
        return TCOMP_ERR_NOMEM;
    }
    bw->capacity = cap;
    return TCOMP_OK;
}

void tcomp_bw_free(tcomp_bitwriter *bw) {
    if (bw == NULL) {
        return;
    }
    free(bw->data);
    memset(bw, 0, sizeof *bw);
}

tcomp_status tcomp_bw_write_bits(tcomp_bitwriter *bw, uint32_t value, unsigned nbits) {
    if (bw == NULL || nbits > TCOMP_BITIO_MAX_BITS) {
        return TCOMP_ERR_INVALID_ARG;
    }
    /* A value wider than its field is a caller bug; masking it would hide
     * the bug and corrupt the stream silently. (nbits == 32 always fits, and
     * shifting a 32-bit value by 32 would be undefined, so skip the check.) */
    if (nbits < 32 && (value >> nbits) != 0) {
        return TCOMP_ERR_INVALID_ARG;
    }
    if (nbits == 0) {
        return TCOMP_OK;
    }

    /* acc holds fewer than 8 pending bits, so after adding at most 32 it holds
     * at most 39: it fits in 64 bits and yields at most 4 whole bytes. Reserve
     * them first so a failed allocation leaves the writer unchanged. */
    unsigned total = bw->acc_bits + nbits;
    tcomp_status st = reserve(bw, total / 8);
    if (st != TCOMP_OK) {
        return st;
    }

    bw->acc = (bw->acc << nbits) | value;
    bw->acc_bits = total;
    while (bw->acc_bits >= 8) {
        bw->acc_bits -= 8;
        bw->data[bw->size++] = (uint8_t)(bw->acc >> bw->acc_bits);
    }
    bw->acc &= ((uint64_t)1 << bw->acc_bits) - 1; /* drop the bits just emitted */
    return TCOMP_OK;
}

tcomp_status tcomp_bw_write_bit(tcomp_bitwriter *bw, unsigned bit) {
    if (bit > 1) {
        return TCOMP_ERR_INVALID_ARG;
    }
    return tcomp_bw_write_bits(bw, bit, 1);
}

tcomp_status tcomp_bw_flush(tcomp_bitwriter *bw) {
    if (bw == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    if (bw->acc_bits == 0) {
        return TCOMP_OK;
    }
    tcomp_status st = reserve(bw, 1);
    if (st != TCOMP_OK) {
        return st;
    }
    /* Shift the pending bits to the top of the byte; the low bits become 0. */
    bw->data[bw->size++] = (uint8_t)(bw->acc << (8 - bw->acc_bits));
    bw->acc = 0;
    bw->acc_bits = 0;
    return TCOMP_OK;
}

size_t tcomp_bw_bit_count(const tcomp_bitwriter *bw) { return bw->size * 8 + bw->acc_bits; }

const uint8_t *tcomp_bw_data(const tcomp_bitwriter *bw) { return bw->data; }

size_t tcomp_bw_size(const tcomp_bitwriter *bw) { return bw->size; }

/* ---- reader ------------------------------------------------------------- */

tcomp_status tcomp_br_init(tcomp_bitreader *br, const uint8_t *data, size_t size) {
    if (br == NULL || (data == NULL && size > 0) || size > SIZE_MAX / 8) {
        return TCOMP_ERR_INVALID_ARG;
    }
    br->data = data;
    br->size = size;
    br->pos = 0;
    return TCOMP_OK;
}

size_t tcomp_br_bits_remaining(const tcomp_bitreader *br) { return br->size * 8 - br->pos; }

uint32_t tcomp_br_peek_bits(const tcomp_bitreader *br, unsigned nbits) {
    if (nbits > TCOMP_BITIO_MAX_BITS) {
        nbits = TCOMP_BITIO_MAX_BITS;
    }
    if (nbits == 0) {
        return 0;
    }
    /* The wanted bits start `offset` bits into byte `index` and span at most
     * 7 + 32 = 39 bits, so 5 bytes always cover them. Load those 5 bytes as
     * one 40-bit big-endian number (0 past the end), then shift the field
     * down to the bottom and mask it. */
    size_t index = br->pos / 8;
    unsigned offset = (unsigned)(br->pos % 8);
    uint64_t window = 0;
    for (size_t i = 0; i < 5; i++) {
        uint8_t byte = (index + i < br->size) ? br->data[index + i] : 0;
        window = (window << 8) | byte;
    }
    window >>= 40 - offset - nbits;
    return (uint32_t)(window & (((uint64_t)1 << nbits) - 1));
}

tcomp_status tcomp_br_skip_bits(tcomp_bitreader *br, size_t nbits) {
    if (br == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    if (nbits > tcomp_br_bits_remaining(br)) {
        return TCOMP_ERR_TRUNCATED;
    }
    br->pos += nbits;
    return TCOMP_OK;
}

tcomp_status tcomp_br_read_bits(tcomp_bitreader *br, unsigned nbits, uint32_t *out) {
    if (br == NULL || out == NULL || nbits > TCOMP_BITIO_MAX_BITS) {
        return TCOMP_ERR_INVALID_ARG;
    }
    if (nbits > tcomp_br_bits_remaining(br)) {
        return TCOMP_ERR_TRUNCATED;
    }
    *out = tcomp_br_peek_bits(br, nbits);
    br->pos += nbits;
    return TCOMP_OK;
}

tcomp_status tcomp_br_read_bit(tcomp_bitreader *br, unsigned *out) {
    if (out == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    uint32_t bit;
    tcomp_status st = tcomp_br_read_bits(br, 1, &bit);
    if (st == TCOMP_OK) {
        *out = (unsigned)bit;
    }
    return st;
}

void tcomp_br_align(tcomp_bitreader *br) { br->pos = (br->pos + 7) & ~(size_t)7; }
