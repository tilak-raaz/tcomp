#include "tcomp/lz77.h"

#include <stdlib.h>

/* ---- token array -------------------------------------------------------- */

void tcomp_lz77_tokens_init(tcomp_lz77_tokens *tokens) {
    tokens->items = NULL;
    tokens->count = 0;
    tokens->capacity = 0;
}

void tcomp_lz77_tokens_free(tcomp_lz77_tokens *tokens) {
    if (tokens == NULL) {
        return;
    }
    free(tokens->items);
    tcomp_lz77_tokens_init(tokens);
}

static tcomp_status push(tcomp_lz77_tokens *tokens, unsigned length, unsigned distance) {
    if (tokens->count == tokens->capacity) {
        size_t cap = tokens->capacity > 0 ? tokens->capacity * 2 : 1024;
        if (cap > SIZE_MAX / sizeof *tokens->items) {
            return TCOMP_ERR_NOMEM;
        }
        tcomp_lz77_token *grown = realloc(tokens->items, cap * sizeof *grown);
        if (grown == NULL) {
            return TCOMP_ERR_NOMEM;
        }
        tokens->items = grown;
        tokens->capacity = cap;
    }
    tokens->items[tokens->count].length = (uint16_t)length;
    tokens->items[tokens->count].distance = (uint16_t)distance;
    tokens->count++;
    return TCOMP_OK;
}

/* ---- shared parsing helpers --------------------------------------------- */

/* Number of equal bytes at a and b, up to max. a may overlap b (a < b):
 * comparing against the input is right because the decoder will have
 * produced exactly these bytes by the time it copies them. */
static size_t common_prefix(const uint8_t *a, const uint8_t *b, size_t max) {
    size_t n = 0;
    while (n < max && a[n] == b[n]) {
        n++;
    }
    return n;
}

/* The best match found so far at one position. */
typedef struct {
    size_t len;
    size_t dist;
} match;

/* Try the candidate `dist` bytes back. Returns 1 when no longer match is
 * possible, so the caller can stop searching.
 *
 * Candidates arrive nearest first and only a strictly longer match replaces
 * the best, so ties keep the smallest distance (cheaper to code once
 * distances are Huffman-coded in M5). */
static int consider(const uint8_t *here, size_t dist, size_t max_len, match *best) {
    const uint8_t *cand = here - dist;
    /* A candidate can only win if it also matches at best->len; checking
     * that byte first rejects most candidates at once. */
    if (cand[best->len] != here[best->len]) {
        return 0;
    }
    size_t l = common_prefix(cand, here, max_len);
    if (l > best->len) {
        best->len = l;
        best->dist = dist;
    }
    return best->len == max_len;
}

/* Append the token for `best` at position i; returns how many bytes it covers. */
static tcomp_status emit(tcomp_lz77_tokens *tokens, const uint8_t *data, size_t i,
                         const match *best, size_t *advance) {
    if (best->len >= TCOMP_LZ77_MIN_MATCH) {
        *advance = best->len;
        return push(tokens, (unsigned)best->len, (unsigned)best->dist);
    }
    *advance = 1;
    return push(tokens, data[i], 0);
}

static size_t min_size(size_t a, size_t b) { return a < b ? a : b; }

static int bad_args(const uint8_t *data, size_t len, size_t window,
                    const tcomp_lz77_tokens *tokens) {
    return tokens == NULL || (data == NULL && len > 0) || window == 0 || window > TCOMP_LZ77_WINDOW;
}

/* ---- naive matcher (reference) ------------------------------------------ */

tcomp_status tcomp_lz77_parse_naive(const uint8_t *data, size_t len, size_t window,
                                    tcomp_lz77_tokens *tokens) {
    if (bad_args(data, len, window, tokens)) {
        return TCOMP_ERR_INVALID_ARG;
    }
    size_t i = 0;
    while (i < len) {
        size_t max_len = min_size(len - i, TCOMP_LZ77_MAX_MATCH);
        size_t max_dist = min_size(i, window);
        match best = {0, 0};
        if (max_len >= TCOMP_LZ77_MIN_MATCH) {
            for (size_t d = 1; d <= max_dist; d++) {
                if (consider(data + i, d, max_len, &best)) {
                    break;
                }
            }
        }
        size_t advance;
        tcomp_status st = emit(tokens, data, i, &best, &advance);
        if (st != TCOMP_OK) {
            return st;
        }
        i += advance;
    }
    return TCOMP_OK;
}

/* ---- hash-chain matcher ------------------------------------------------- */

/* head[h] is the most recent position whose next 3 bytes hash to h.
 * prev[p % WINDOW] is the position before p with the same hash. Following
 * head -> prev -> prev ... visits same-hash positions nearest first.
 *
 * prev is a ring of WINDOW entries: the slot for p is reused by p + WINDOW,
 * but by then p is outside the window and the walk has already stopped. */
enum { HASH_BITS = 15, HASH_SIZE = 1 << HASH_BITS, RING_MASK = TCOMP_LZ77_WINDOW - 1 };
#define NO_POS SIZE_MAX

_Static_assert((TCOMP_LZ77_WINDOW & (TCOMP_LZ77_WINDOW - 1)) == 0, "window must be 2^k");

/* Multiplicative (Fibonacci) hashing: the top bits of v * 2^32/phi are well
 * mixed even when the 3 input bytes differ only slightly. */
static size_t hash3(const uint8_t *p) {
    uint32_t v = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    return (v * 2654435761u) >> (32 - HASH_BITS);
}

tcomp_status tcomp_lz77_parse(const uint8_t *data, size_t len, size_t window, size_t max_chain,
                              tcomp_lz77_tokens *tokens) {
    if (bad_args(data, len, window, tokens)) {
        return TCOMP_ERR_INVALID_ARG;
    }
    size_t *head = malloc(HASH_SIZE * sizeof *head);
    size_t *prev = malloc(TCOMP_LZ77_WINDOW * sizeof *prev);
    if (head == NULL || prev == NULL) {
        free(head);
        free(prev);
        return TCOMP_ERR_NOMEM;
    }
    for (size_t h = 0; h < HASH_SIZE; h++) {
        head[h] = NO_POS;
    }
    if (max_chain == TCOMP_LZ77_UNLIMITED_CHAIN) {
        max_chain = SIZE_MAX;
    }

    tcomp_status st = TCOMP_OK;
    size_t i = 0, indexed = 0; /* positions [0, indexed) are in the chains */
    while (i < len && st == TCOMP_OK) {
        size_t max_len = min_size(len - i, TCOMP_LZ77_MAX_MATCH);
        size_t max_dist = min_size(i, window);
        match best = {0, 0};

        if (max_len >= TCOMP_LZ77_MIN_MATCH) {
            size_t cand = head[hash3(data + i)];
            for (size_t steps = 0; cand != NO_POS && steps < max_chain; steps++) {
                if (i - cand > max_dist) {
                    break; /* chains only get older from here */
                }
                if (consider(data + i, i - cand, max_len, &best)) {
                    break;
                }
                cand = prev[cand & RING_MASK];
            }
        }

        size_t advance;
        st = emit(tokens, data, i, &best, &advance);
        i += advance;

        /* Index every position just covered, including those inside a
         * match: later matches may start at any of them. */
        for (; indexed < i && indexed + TCOMP_LZ77_MIN_MATCH <= len; indexed++) {
            size_t h = hash3(data + indexed);
            prev[indexed & RING_MASK] = head[h];
            head[h] = indexed;
        }
    }
    free(head);
    free(prev);
    return st;
}

/* ---- expansion ---------------------------------------------------------- */

tcomp_status tcomp_lz77_expand(const tcomp_lz77_token *tokens, size_t count, uint8_t *out,
                               size_t cap, size_t *out_len) {
    if ((tokens == NULL && count > 0) || (out == NULL && cap > 0) || out_len == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    size_t pos = 0;
    for (size_t t = 0; t < count; t++) {
        unsigned length = tokens[t].length;
        unsigned distance = tokens[t].distance;
        if (distance == 0) {
            if (length > 255 || pos == cap) {
                return TCOMP_ERR_CORRUPT;
            }
            out[pos++] = (uint8_t)length;
            continue;
        }
        if (length < TCOMP_LZ77_MIN_MATCH || length > TCOMP_LZ77_MAX_MATCH ||
            distance > TCOMP_LZ77_WINDOW || distance > pos || length > cap - pos) {
            return TCOMP_ERR_CORRUPT;
        }
        /* Byte by byte, not memcpy: when distance < length the source
         * overlaps bytes this same copy is producing. */
        for (unsigned k = 0; k < length; k++, pos++) {
            out[pos] = out[pos - distance];
        }
    }
    *out_len = pos;
    return TCOMP_OK;
}
