/* tcomp benchmark harness.
 *
 *   usage: tcomp-bench [--quick] FILE...
 *
 * Everything runs in one process on data already in memory, so the numbers
 * measure the code, not process start-up or disk. Each measurement is the
 * best of several runs (the least disturbed by other activity on the
 * machine). Build and run with `make bench`, which uses a release build and
 * the Canterbury corpus.
 *
 * Section 1, LZ77 matchers: parse every file with the naive matcher and with
 * hash chains of several lengths. Reports parse throughput, speedup over
 * naive, and the size the tokens take in the LZ77 method's fixed-width format, so the
 * cost of a shorter chain is visible next to its speed.
 *
 * Section 2, methods end to end: tcomp_compress_stream() and
 * tcomp_decompress_stream() per method through in-memory FILE streams, with
 * every result decompressed and compared byte for byte. Decompression
 * includes checking the CRC-32.
 *
 * Section 3, CRC-32: checksum throughput on its own, to show what the
 * integrity check costs relative to decompression.
 *
 * --quick skips the naive matcher (about 14 s on the corpus) and uses fewer
 * repetitions.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tcomp/container.h"
#include "tcomp/crc32.h"
#include "tcomp/lz77.h"

typedef struct {
    const char *name;
    uint8_t *data;
    size_t len;
} input;

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void die(const char *what, const char *detail) {
    fprintf(stderr, "tcomp-bench: %s: %s\n", what, detail);
    exit(1);
}

static input load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        die("cannot open", path);
    }
    size_t cap = 1 << 20, len = 0;
    uint8_t *buf = malloc(cap);
    size_t n;
    while (buf != NULL && (n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    if (buf == NULL || ferror(f)) {
        die("cannot read", path);
    }
    fclose(f);
    const char *slash = strrchr(path, '/');
    input in = {slash ? slash + 1 : path, buf, len};
    return in;
}

/* ---- section 1: matchers ------------------------------------------------ */

enum { NAIVE = -1 };

/* Fixed-width size of the tokens (9-bit literals, 24-bit matches), in bytes. */
static uint64_t token_bytes(const tcomp_lz77_tokens *t) {
    uint64_t bits = 0;
    for (size_t i = 0; i < t->count; i++) {
        bits += t->items[i].distance == 0 ? 9 : 24;
    }
    return (bits + 7) / 8;
}

static void bench_matchers(const input *inputs, int count, size_t total, int quick) {
    const long chains[] = {NAIVE, 1, 4, 16, 64, 128, 256, 1024, TCOMP_LZ77_UNLIMITED_CHAIN};
    double naive_time = 0;

    printf("## LZ77 matchers (greedy parse, 32 KB window)\n\n");
    printf("| Matcher | Parse time | Parse MB/s | Speedup vs naive | Tokens (fixed-width) | "
           "Ratio |\n");
    printf("| --- | ---: | ---: | ---: | ---: | ---: |\n");
    for (size_t c = 0; c < sizeof chains / sizeof *chains; c++) {
        long chain = chains[c];
        if (chain == NAIVE && quick) {
            continue;
        }
        int reps = (chain == NAIVE || chain == TCOMP_LZ77_UNLIMITED_CHAIN) ? 1 : (quick ? 2 : 5);
        double best = 1e30;
        uint64_t bytes = 0;
        for (int r = 0; r < reps; r++) {
            bytes = 0;
            double t0 = now();
            for (int i = 0; i < count; i++) {
                tcomp_lz77_tokens t;
                tcomp_lz77_tokens_init(&t);
                tcomp_status st = chain == NAIVE
                                      ? tcomp_lz77_parse_naive(inputs[i].data, inputs[i].len,
                                                               TCOMP_LZ77_WINDOW, &t)
                                      : tcomp_lz77_parse(inputs[i].data, inputs[i].len,
                                                         TCOMP_LZ77_WINDOW, (size_t)chain, &t);
                if (st != TCOMP_OK) {
                    die("parse failed", inputs[i].name);
                }
                bytes += token_bytes(&t);
                tcomp_lz77_tokens_free(&t);
            }
            double dt = now() - t0;
            best = dt < best ? dt : best;
        }
        if (chain == NAIVE) {
            naive_time = best;
        }
        char label[32], speedup[32];
        if (chain == NAIVE) {
            snprintf(label, sizeof label, "naive");
        } else if (chain == TCOMP_LZ77_UNLIMITED_CHAIN) {
            snprintf(label, sizeof label, "hash chain, unlimited");
        } else {
            snprintf(label, sizeof label, "hash chain, %ld%s", chain,
                     chain == TCOMP_LZ77_DEFAULT_CHAIN ? " (default)" : "");
        }
        if (naive_time > 0) {
            snprintf(speedup, sizeof speedup, "%.0fx", naive_time / best);
        } else {
            snprintf(speedup, sizeof speedup, "-");
        }
        printf("| %s | %.3f s | %.2f | %s | %llu | %.3f |\n", label, best, total / best / 1e6,
               speedup, (unsigned long long)bytes, (double)total / (double)bytes);
        fflush(stdout);
    }
    printf("\n");
}

/* ---- section 2: methods end to end -------------------------------------- */

typedef struct {
    char *buf;
    size_t len;
} memout;

/* Compress `in` with `method` into a fresh memory buffer. */
static double timed_compress(const input *in, tcomp_method method, memout *out) {
    FILE *src = fmemopen(in->data, in->len > 0 ? in->len : 1, "rb");
    FILE *dst = open_memstream(&out->buf, &out->len);
    if (src == NULL || dst == NULL) {
        die("stream", in->name);
    }
    if (in->len == 0) {
        fgetc(src); /* fmemopen cannot be empty; consume the dummy byte */
    }
    double t0 = now();
    tcomp_status st = tcomp_compress_stream(src, dst, method);
    fclose(dst);
    double dt = now() - t0;
    fclose(src);
    if (st != TCOMP_OK) {
        die("compress failed", in->name);
    }
    return dt;
}

static double timed_decompress(const input *in, const memout *packed) {
    FILE *src = fmemopen(packed->buf, packed->len, "rb");
    char *buf = NULL;
    size_t len = 0;
    FILE *dst = open_memstream(&buf, &len);
    if (src == NULL || dst == NULL) {
        die("stream", in->name);
    }
    double t0 = now();
    tcomp_status st = tcomp_decompress_stream(src, dst);
    fclose(dst);
    double dt = now() - t0;
    fclose(src);
    if (st != TCOMP_OK || len != in->len || (len > 0 && memcmp(buf, in->data, len) != 0)) {
        die("roundtrip mismatch", in->name);
    }
    free(buf);
    return dt;
}

static void bench_methods(const input *inputs, int count, size_t total, int quick) {
    const struct {
        const char *name;
        tcomp_method method;
    } methods[] = {{"store", TCOMP_METHOD_STORE},
                   {"huffman", TCOMP_METHOD_HUFFMAN},
                   {"lz77", TCOMP_METHOD_LZ77},
                   {"lzh", TCOMP_METHOD_LZH},
                   {"auto", TCOMP_METHOD_AUTO}};
    int reps = quick ? 2 : 5;

    printf("## Methods end to end (in memory, every result verified)\n\n");
    printf("| Method | Compressed | Ratio | Compress MB/s | Decompress MB/s |\n");
    printf("| --- | ---: | ---: | ---: | ---: |\n");
    for (size_t m = 0; m < sizeof methods / sizeof *methods; m++) {
        double best_c = 1e30, best_d = 1e30;
        size_t packed_total = 0;
        for (int r = 0; r < reps; r++) {
            double tc = 0, td = 0;
            packed_total = 0;
            for (int i = 0; i < count; i++) {
                memout packed = {NULL, 0};
                tc += timed_compress(&inputs[i], methods[m].method, &packed);
                td += timed_decompress(&inputs[i], &packed);
                packed_total += packed.len;
                free(packed.buf);
            }
            best_c = tc < best_c ? tc : best_c;
            best_d = td < best_d ? td : best_d;
        }
        printf("| %s | %zu | %.3f | %.1f | %.1f |\n", methods[m].name, packed_total,
               (double)total / (double)packed_total, total / best_c / 1e6, total / best_d / 1e6);
        fflush(stdout);
    }
    printf("\n");
}

static void bench_crc32(const input *inputs, int count, size_t total, int quick) {
    int reps = quick ? 3 : 10;
    double best = 1e30;
    volatile uint32_t sink = 0; /* keeps the work from being optimised away */
    for (int r = 0; r < reps; r++) {
        double t = now();
        for (int i = 0; i < count; i++) {
            sink ^= tcomp_crc32_update(0, inputs[i].data, inputs[i].len);
        }
        t = now() - t;
        best = t < best ? t : best;
    }
    (void)sink;
    printf("## CRC-32 (slicing-by-8)\n\n");
    printf("| Bytes | Time | MB/s |\n| ---: | ---: | ---: |\n");
    printf("| %zu | %.4f s | %.0f |\n\n", total, best, total / best / 1e6);
}

int main(int argc, char **argv) {
    int quick = 0, first = 1;
    if (argc > 1 && strcmp(argv[1], "--quick") == 0) {
        quick = 1;
        first = 2;
    }
    if (first >= argc) {
        fprintf(stderr, "usage: tcomp-bench [--quick] FILE...\n");
        return 2;
    }
    input *inputs = malloc((size_t)argc * sizeof *inputs);
    int count = 0;
    size_t total = 0;
    for (int a = first; a < argc; a++) {
        const char *base = strrchr(argv[a], '/');
        if (strcmp(base ? base + 1 : argv[a], "SHA1SUM") == 0) {
            continue;
        }
        inputs[count] = load(argv[a]);
        total += inputs[count].len;
        count++;
    }
    printf("# tcomp %s benchmark: %d files, %zu bytes%s\n\n", TCOMP_VERSION_STRING, count, total,
           quick ? " (quick)" : "");

    bench_matchers(inputs, count, total, quick);
    bench_methods(inputs, count, total, quick);
    bench_crc32(inputs, count, total, quick);

    for (int i = 0; i < count; i++) {
        free(inputs[i].data);
    }
    free(inputs);
    return 0;
}
