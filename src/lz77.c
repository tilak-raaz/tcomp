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

/* ---- naive greedy parser ------------------------------------------------ */

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

tcomp_status tcomp_lz77_parse(const uint8_t *data, size_t len, size_t window,
                              tcomp_lz77_tokens *tokens) {
    if (tokens == NULL || (data == NULL && len > 0) || window == 0 || window > TCOMP_LZ77_WINDOW) {
        return TCOMP_ERR_INVALID_ARG;
    }

    size_t i = 0;
    while (i < len) {
        size_t max_len = len - i < TCOMP_LZ77_MAX_MATCH ? len - i : TCOMP_LZ77_MAX_MATCH;
        size_t max_dist = i < window ? i : window;
        size_t best_len = 0, best_dist = 0;

        if (max_len >= TCOMP_LZ77_MIN_MATCH) {
            /* Nearest candidates first, so ties keep the smallest distance
             * (cheaper to code once distances are Huffman-coded in M4). */
            for (size_t d = 1; d <= max_dist; d++) {
                const uint8_t *cand = data + i - d;
                /* A candidate can only win if it also matches at best_len;
                 * checking that byte first rejects most candidates at once. */
                if (cand[best_len] != data[i + best_len]) {
                    continue;
                }
                size_t l = common_prefix(cand, data + i, max_len);
                if (l > best_len) {
                    best_len = l;
                    best_dist = d;
                    if (l == max_len) {
                        break; /* cannot do better */
                    }
                }
            }
        }

        tcomp_status st;
        if (best_len >= TCOMP_LZ77_MIN_MATCH) {
            st = push(tokens, (unsigned)best_len, (unsigned)best_dist);
            i += best_len;
        } else {
            st = push(tokens, data[i], 0);
            i++;
        }
        if (st != TCOMP_OK) {
            return st;
        }
    }
    return TCOMP_OK;
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
