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

typedef tcomp_status (*stream_fn)(FILE *in, FILE *out);

static void print_usage(FILE *to) {
    fputs("usage: tcomp compress   <input> <output>\n"
          "       tcomp decompress <input> <output>\n"
          "       tcomp --version\n"
          "       tcomp --help\n"
          "\n"
          "Use - for <input> or <output> to read stdin or write stdout.\n",
          to);
}

static int is_dash(const char *path) { return strcmp(path, "-") == 0; }

/* Open input/output, run fn, close both. On failure, delete a partially
 * written output file so a broken result is never left behind. */
static int run(stream_fn fn, const char *in_path, const char *out_path) {
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

    tcomp_status st = fn(in, out);

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
    if (argc == 4 && strcmp(argv[1], "compress") == 0) {
        return run(tcomp_compress_stream, argv[2], argv[3]);
    }
    if (argc == 4 && strcmp(argv[1], "decompress") == 0) {
        return run(tcomp_decompress_stream, argv[2], argv[3]);
    }
    print_usage(stderr);
    return EXIT_USAGE;
}
