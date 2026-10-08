/**
 * @file container.h
 * @brief Reading and writing the .tcmp container around compressed data.
 *
 * A .tcmp file is a small header naming the compression method, followed by
 * that method's payload. Decompression reads the method from the header, so
 * callers never need to know how a file was compressed.
 *
 * The exact byte layout is specified in docs/FORMAT.md.
 */
#ifndef TCOMP_CONTAINER_H
#define TCOMP_CONTAINER_H

#include <stdio.h>

#include "tcomp/status.h"

/** Compression methods. STORE and HUFFMAN values are part of the file format. */
typedef enum {
    TCOMP_METHOD_STORE = 0,   /**< No compression; payload is the raw input. */
    TCOMP_METHOD_HUFFMAN = 1, /**< Order-0 canonical Huffman coding of bytes. */
    TCOMP_METHOD_AUTO = 255   /**< Request only: use whichever method gives the smaller
                                   file. Never written to a file. */
} tcomp_method;

/**
 * @brief Compress everything readable from @p in and write a .tcmp stream to @p out.
 *
 * Reads until EOF. Does not close either stream. STORE streams its input with
 * constant memory; HUFFMAN and AUTO read the whole input into memory first,
 * because Huffman needs symbol counts before it can encode (blocks in M7
 * remove this limit).
 *
 * @param in     Open stream, read in binary mode. Must not be NULL.
 * @param out    Open stream, written in binary mode. Must not be NULL.
 * @param method STORE, HUFFMAN, or AUTO (recommended). AUTO picks HUFFMAN
 *               only when it is strictly smaller, so the output is never more
 *               than the header larger than the input.
 * @return TCOMP_OK, TCOMP_ERR_INVALID_ARG, TCOMP_ERR_NOMEM or TCOMP_ERR_IO.
 */
tcomp_status tcomp_compress_stream(FILE *in, FILE *out, tcomp_method method);

/**
 * @brief Read a .tcmp stream from @p in and write the original bytes to @p out.
 *
 * Treats the input as untrusted: every field is validated before use, and no
 * allocation is sized from a value read from the file. Does not close either
 * stream. On error, @p out may already hold partial output; the caller should
 * discard it.
 *
 * @param in  Open stream, read in binary mode. Must not be NULL.
 * @param out Open stream, written in binary mode. Must not be NULL.
 * @return TCOMP_OK, or one of TCOMP_ERR_INVALID_ARG, TCOMP_ERR_IO,
 *         TCOMP_ERR_NOMEM, TCOMP_ERR_BAD_MAGIC, TCOMP_ERR_BAD_VERSION,
 *         TCOMP_ERR_BAD_METHOD, TCOMP_ERR_TRUNCATED, TCOMP_ERR_CORRUPT.
 */
tcomp_status tcomp_decompress_stream(FILE *in, FILE *out);

#endif /* TCOMP_CONTAINER_H */
