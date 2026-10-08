#include "tcomp/huffman.h"

#include <stdlib.h>
#include <string.h>

/* Decoder table entries pack (symbol << 4) | length into 16 bits. */
_Static_assert(TCOMP_HUFF_MAX_SYMBOLS * 16u <= 65536u, "symbol must fit in 12 bits");
_Static_assert(TCOMP_HUFF_MAX_BITS <= 15u, "length must fit in 4 bits");

/* ---- building code lengths ---------------------------------------------- */

/* Min-heap order on tree nodes: lighter first; equal weights by node index,
 * which makes the result deterministic. */
static int node_less(const uint64_t *w, size_t a, size_t b) {
    return w[a] < w[b] || (w[a] == w[b] && a < b);
}

static void heap_push(size_t *heap, size_t *n, const uint64_t *w, size_t node) {
    size_t i = (*n)++;
    heap[i] = node;
    while (i > 0) {
        size_t up = (i - 1) / 2;
        if (!node_less(w, heap[i], heap[up])) {
            break;
        }
        size_t tmp = heap[i];
        heap[i] = heap[up];
        heap[up] = tmp;
        i = up;
    }
}

static size_t heap_pop(size_t *heap, size_t *n, const uint64_t *w) {
    size_t top = heap[0];
    heap[0] = heap[--(*n)];
    size_t i = 0;
    for (;;) {
        size_t left = 2 * i + 1, right = left + 1, min = i;
        if (left < *n && node_less(w, heap[left], heap[min])) {
            min = left;
        }
        if (right < *n && node_less(w, heap[right], heap[min])) {
            min = right;
        }
        if (min == i) {
            break;
        }
        size_t tmp = heap[i];
        heap[i] = heap[min];
        heap[min] = tmp;
        i = min;
    }
    return top;
}

/* Build a Huffman tree over leaves 0..m-1 (m >= 2) with weights w[0..m-1].
 * Internal nodes get indices m..2m-2 in creation order, so a node's parent
 * always has a larger index than the node, and the root is 2m-2.
 * Fills depth[] for every node and returns the deepest leaf's depth. */
static unsigned build_tree(size_t m, uint64_t *w, size_t *parent, size_t *heap, uint16_t *depth) {
    size_t n = 0;
    for (size_t i = 0; i < m; i++) {
        heap_push(heap, &n, w, i);
    }
    size_t next = m;
    while (n > 1) {
        size_t a = heap_pop(heap, &n, w);
        size_t b = heap_pop(heap, &n, w);
        w[next] = w[a] + w[b];
        parent[a] = next;
        parent[b] = next;
        heap_push(heap, &n, w, next);
        next++;
    }

    size_t root = next - 1;
    depth[root] = 0;
    for (size_t i = root; i-- > 0;) { /* parents before children */
        depth[i] = (uint16_t)(depth[parent[i]] + 1);
    }
    unsigned deepest = 0;
    for (size_t i = 0; i < m; i++) {
        if (depth[i] > deepest) {
            deepest = depth[i];
        }
    }
    return deepest;
}

tcomp_status tcomp_huff_build_lengths(const uint64_t *freqs, size_t nsyms, unsigned max_bits,
                                      uint8_t *lengths) {
    if (freqs == NULL || lengths == NULL || nsyms == 0 || nsyms > TCOMP_HUFF_MAX_SYMBOLS ||
        max_bits == 0 || max_bits > TCOMP_HUFF_MAX_BITS) {
        return TCOMP_ERR_INVALID_ARG;
    }

    /* Count used symbols and guard the weight sums against overflow. */
    size_t m = 0;
    uint64_t total = 0;
    for (size_t s = 0; s < nsyms; s++) {
        if (freqs[s] > UINT64_MAX - total) {
            return TCOMP_ERR_INVALID_ARG;
        }
        total += freqs[s];
        m += freqs[s] > 0;
    }
    if (m > ((size_t)1 << max_bits)) {
        return TCOMP_ERR_INVALID_ARG; /* cannot give m symbols distinct codes */
    }

    memset(lengths, 0, nsyms);
    if (m == 0) {
        return TCOMP_OK;
    }
    if (m == 1) {
        for (size_t s = 0; s < nsyms; s++) {
            if (freqs[s] > 0) {
                lengths[s] = 1;
            }
        }
        return TCOMP_OK;
    }

    size_t nodes = 2 * m - 1;
    uint64_t *w = malloc(nodes * sizeof *w);
    size_t *parent = malloc(nodes * sizeof *parent);
    size_t *heap = malloc(m * sizeof *heap);
    size_t *sym = malloc(m * sizeof *sym);
    uint16_t *depth = malloc(nodes * sizeof *depth);
    tcomp_status st = TCOMP_ERR_NOMEM;
    if (w == NULL || parent == NULL || heap == NULL || sym == NULL || depth == NULL) {
        goto done;
    }

    for (size_t s = 0, leaf = 0; s < nsyms; s++) {
        if (freqs[s] > 0) {
            sym[leaf] = s;
            w[leaf] = freqs[s];
            leaf++;
        }
    }

    /* Too deep? Halve every weight (rounding up so none becomes 0) and
     * rebuild. Weights converge to all 1s, whose tree is balanced with depth
     * ceil(log2 m) <= max_bits, so the loop always ends. */
    while (build_tree(m, w, parent, heap, depth) > max_bits) {
        for (size_t i = 0; i < m; i++) {
            w[i] = (w[i] >> 1) + (w[i] & 1);
        }
    }

    for (size_t i = 0; i < m; i++) {
        lengths[sym[i]] = (uint8_t)depth[i];
    }
    st = TCOMP_OK;

done:
    free(w);
    free(parent);
    free(heap);
    free(sym);
    free(depth);
    return st;
}

/* ---- canonical codes ---------------------------------------------------- */

tcomp_status tcomp_huff_assign_codes(const uint8_t *lengths, size_t nsyms, uint16_t *codes) {
    if (lengths == NULL || codes == NULL || nsyms == 0 || nsyms > TCOMP_HUFF_MAX_SYMBOLS) {
        return TCOMP_ERR_INVALID_ARG;
    }

    unsigned count[TCOMP_HUFF_MAX_BITS + 1] = {0};
    for (size_t s = 0; s < nsyms; s++) {
        if (lengths[s] > TCOMP_HUFF_MAX_BITS) {
            return TCOMP_ERR_CORRUPT;
        }
        count[lengths[s]]++;
    }
    count[0] = 0;

    /* Kraft inequality: at each length L there are 2^L bit patterns, minus
     * those already taken by shorter codes. Using more than exist means no
     * prefix code has these lengths. */
    long available = 1;
    for (unsigned len = 1; len <= TCOMP_HUFF_MAX_BITS; len++) {
        available = available * 2 - (long)count[len];
        if (available < 0) {
            return TCOMP_ERR_CORRUPT;
        }
    }

    /* First code of each length (RFC 1951, 3.2.2 step 2). */
    unsigned next[TCOMP_HUFF_MAX_BITS + 1] = {0};
    unsigned code = 0;
    for (unsigned len = 1; len <= TCOMP_HUFF_MAX_BITS; len++) {
        code = (code + count[len - 1]) << 1;
        next[len] = code;
    }

    for (size_t s = 0; s < nsyms; s++) {
        codes[s] = lengths[s] > 0 ? (uint16_t)next[lengths[s]]++ : 0;
    }
    return TCOMP_OK;
}

/* ---- decoding ----------------------------------------------------------- */

tcomp_status tcomp_huff_decoder_init(tcomp_huff_decoder *dec, const uint8_t *lengths,
                                     size_t nsyms) {
    if (dec == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    dec->table = NULL;
    dec->bits = 0;

    uint16_t codes[TCOMP_HUFF_MAX_SYMBOLS];
    tcomp_status st = tcomp_huff_assign_codes(lengths, nsyms, codes);
    if (st != TCOMP_OK) {
        return st;
    }

    unsigned bits = 0;
    for (size_t s = 0; s < nsyms; s++) {
        if (lengths[s] > bits) {
            bits = lengths[s];
        }
    }

    size_t entries = (size_t)1 << bits;
    uint16_t *table = calloc(entries, sizeof *table);
    if (table == NULL) {
        return TCOMP_ERR_NOMEM;
    }

    /* A code of length L owns every table index that starts with it: the
     * 2^(bits - L) indices from code << (bits - L) onwards. */
    for (size_t s = 0; s < nsyms; s++) {
        unsigned len = lengths[s];
        if (len == 0) {
            continue;
        }
        size_t first = (size_t)codes[s] << (bits - len);
        size_t span = (size_t)1 << (bits - len);
        uint16_t entry = (uint16_t)((s << 4) | len);
        for (size_t i = 0; i < span; i++) {
            table[first + i] = entry;
        }
    }

    dec->table = table;
    dec->bits = bits;
    return TCOMP_OK;
}

void tcomp_huff_decoder_free(tcomp_huff_decoder *dec) {
    if (dec == NULL) {
        return;
    }
    free(dec->table);
    dec->table = NULL;
    dec->bits = 0;
}

tcomp_status tcomp_huff_decode_symbol(const tcomp_huff_decoder *dec, tcomp_bitreader *br,
                                      unsigned *sym) {
    if (dec == NULL || dec->table == NULL || br == NULL || sym == NULL) {
        return TCOMP_ERR_INVALID_ARG;
    }
    /* Peek pads with zeros past the end, so near the end of the input this
     * still finds the right entry; skip then reports truncation if the code
     * is longer than the bits that really remain. */
    uint16_t entry = dec->table[tcomp_br_peek_bits(br, dec->bits)];
    unsigned len = entry & 0xFu;
    if (len == 0) {
        return TCOMP_ERR_CORRUPT;
    }
    tcomp_status st = tcomp_br_skip_bits(br, len);
    if (st != TCOMP_OK) {
        return st;
    }
    *sym = entry >> 4;
    return TCOMP_OK;
}
