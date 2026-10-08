/* tcomp command-line interface.
 *
 * This is the only file allowed to print, call exit(), or decide exit codes.
 * Everything else lives in the library (src/ minus this file) and returns
 * tcomp_status values.
 *
 * Exit codes: 0 success, 1 runtime error (bad input, I/O), 2 usage error.
 */
#include <stdio.h>
#include <string.h>

#include "tcomp/container.h"
#include "tcomp/status.h"

enum { EXIT_OK = 0, EXIT_RUNTIME = 1, EXIT_USAGE = 2 };

typedef enum { MODE_COMPRESS, MODE_DECOMPRESS } run_mode;

static void print_usage(FILE *to) {
    fputs("usage: tcomp compress [-m METHOD] <input> <output>\n"
          "       tcomp decompress <input> <output>\n"
          "       tcomp --version\n"
          "       tcomp --help\n"
          "\n"
          "METHOD is one of:\n"
          "  auto      smaller of huffman and store (default)\n"
          "  huffman   canonical Huffman coding of bytes\n"
          "  lz77      LZ77 back-references (slow: naive matcher until M6)\n"
          "  store     no compression\n"
          "\n"
          "Use - for <input> or <output> to read stdin or write stdout.\n",
          to);
}

static int parse_method(const char *name, tcomp_method *method) {
    if (strcmp(name, "auto") == 0) {
        *method = TCOMP_METHOD_AUTO;
    } else if (strcmp(name, "huffman") == 0) {
        *method = TCOMP_METHOD_HUFFMAN;
    } else if (strcmp(name, "lz77") == 0) {
        *method = TCOMP_METHOD_LZ77;
    } else if (strcmp(name, "store") == 0) {
        *method = TCOMP_METHOD_STORE;
    } else {
        return 0;
    }
    return 1;
}

static int is_dash(const char *path) { return strcmp(path, "-") == 0; }

/* Open input/output, compress or decompress, close both. On failure, delete
 * a partially written output file so a broken result is never left behind. */
static int run(run_mode mode, tcomp_method method, const char *in_path, const char *out_path) {
    FILE *in = is_dash(in_path) ? stdin : fopen(in_path, "rb");
    if (in == NULL) {
        fprintf(stderr, "tcomp: cannot open '%s' for reading\n", in_path);
        return EXIT_RUNTIME;
    }
    FILE *out = is_dash(out_path) ? stdout : fopen(out_path, "wb");
    if (out == NULL) {
        fprintf(stderr, "tcomp: cannot open '%s' for writing\n", out_path);
        if (in != stdin) {
            fclose(in);
        }
        return EXIT_RUNTIME;
    }

    tcomp_status st = (mode == MODE_COMPRESS) ? tcomp_compress_stream(in, out, method)
                                              : tcomp_decompress_stream(in, out);

    if (in != stdin) {
        fclose(in);
    }
    /* fclose flushes; a failed flush (disk full) is a real write error. */
    int close_failed = (out == stdout) ? (fflush(out) != 0) : (fclose(out) != 0);
    if (st == TCOMP_OK && close_failed) {
        st = TCOMP_ERR_IO;
    }

    if (st != TCOMP_OK) {
        fprintf(stderr, "tcomp: %s: %s\n", in_path, tcomp_strerror(st));
        if (out != stdout) {
            remove(out_path);
        }
        return EXIT_RUNTIME;
    }
    return EXIT_OK;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("tcomp %s\n", TCOMP_VERSION_STRING);
        return EXIT_OK;
    }
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_usage(stdout);
        return EXIT_OK;
    }
    if (argc == 4 && strcmp(argv[1], "compress") == 0 && strcmp(argv[2], "-m") != 0) {
        return run(MODE_COMPRESS, TCOMP_METHOD_AUTO, argv[2], argv[3]);
    }
    if (argc == 6 && strcmp(argv[1], "compress") == 0 && strcmp(argv[2], "-m") == 0) {
        tcomp_method method;
        if (!parse_method(argv[3], &method)) {
            fprintf(stderr, "tcomp: unknown method '%s'\n\n", argv[3]);
            print_usage(stderr);
            return EXIT_USAGE;
        }
        return run(MODE_COMPRESS, method, argv[4], argv[5]);
    }
    if (argc == 4 && strcmp(argv[1], "decompress") == 0) {
        return run(MODE_DECOMPRESS, TCOMP_METHOD_AUTO, argv[2], argv[3]);
    }
    print_usage(stderr);
    return EXIT_USAGE;
}
