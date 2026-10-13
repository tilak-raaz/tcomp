/**
 * @file container.h
 * @brief Reading and writing the .tcmp container around compressed data.
 *
 * A .tcmp file (format version 1) is a 15-byte header naming the compression
 * method and the original size, that method's payload, and a 4-byte trailer
 * holding the CRC-32 of the original data. Decompression reads the method
 * from the header, so callers never need to know how a file was compressed,
 * and checks the CRC, so damaged files are reported instead of silently
 * producing wrong output.
 *
 * The exact byte layout is specified in docs/FORMAT.md.
 */
#ifndef TCOMP_CONTAINER_H
#define TCOMP_CONTAINER_H

#include <stdint.h>
#include <stdio.h>

#include "tcomp/status.h"

/** Compression methods. Values 0-254 are part of the file format. */
typedef enum {
    TCOMP_METHOD_STORE = 0,   /**< No compression; payload is the raw input. */
    TCOMP_METHOD_HUFFMAN = 1, /**< Order-0 canonical Huffman coding of bytes. */
    TCOMP_METHOD_LZ77 = 2,    /**< LZ77 tokens in fixed-width fields (no entropy coding). */
    TCOMP_METHOD_LZH = 3,     /**< LZ77 tokens Huffman-coded, DEFLATE-style. */
    TCOMP_METHOD_AUTO = 255   /**< Request only: the smallest of the methods above.
                                   Never written to a file. */
} tcomp_method;

/** Header size plus trailer size: the fixed cost of a .tcmp file. */
#define TCOMP_FILE_OVERHEAD 19

/** What tcomp_read_info() learns from a file's header and trailer. */
typedef struct {
    unsigned version;         /**< Format version (always 1 for files this build accepts). */
    tcomp_method method;      /**< Method the payload was written with. */
    uint64_t original_size;   /**< Size of the data before compression, in bytes. */
    uint64_t compressed_size; /**< Size of the whole .tcmp file, in bytes. */
    uint32_t crc32;           /**< CRC-32 of the original data, as stored in the trailer. */
} tcomp_info;

/**
 * @brief Name of a method as the CLI spells it ("store", "huffman", ...).
 * @return A static string; "unknown" for values outside the enum.
 */
const char *tcomp_method_name(tcomp_method method);

/**
 * @brief Compress everything readable from @p in and write a .tcmp stream to @p out.
 *
 * Reads until EOF. Does not close either stream. The whole input is read into
 * memory first, because the header records its size (blocks in M7 remove this
 * limit).
 *
 * @param in     Open stream, read in binary mode. Must not be NULL.
 * @param out    Open stream, written in binary mode. Must not be NULL.
 * @param method STORE, HUFFMAN, LZ77, LZH, or AUTO (recommended). AUTO computes
 *               the exact size every method would produce and writes the
 *               smallest, preferring the simpler method on ties, so the
 *               output is never more than TCOMP_FILE_OVERHEAD bytes larger
 *               than the input.
 * @return TCOMP_OK, TCOMP_ERR_INVALID_ARG, TCOMP_ERR_NOMEM or TCOMP_ERR_IO.
 */
tcomp_status tcomp_compress_stream(FILE *in, FILE *out, tcomp_method method);

/**
 * @brief Read a .tcmp stream from @p in and write the original bytes to @p out.
 *
 * Treats the input as untrusted: every field is validated before use, and no
 * allocation is sized from a value read from the file. The CRC-32 is checked
 * after the last byte is decoded. Does not close either stream.
 *
 * On error, @p out may already hold partial (or, on TCOMP_ERR_CHECKSUM,
 * complete but wrong) output; the caller must discard it. The CLI deletes
 * the output file.
 *
 * @param in  Open stream, read in binary mode. Must not be NULL.
 * @param out Open stream, written in binary mode. Must not be NULL.
 * @return TCOMP_OK, or one of TCOMP_ERR_INVALID_ARG, TCOMP_ERR_IO,
 *         TCOMP_ERR_NOMEM, TCOMP_ERR_BAD_MAGIC, TCOMP_ERR_BAD_VERSION,
 *         TCOMP_ERR_BAD_METHOD, TCOMP_ERR_TRUNCATED, TCOMP_ERR_CORRUPT,
 *         TCOMP_ERR_CHECKSUM.
 */
tcomp_status tcomp_decompress_stream(FILE *in, FILE *out);

/**
 * @brief Fully decode a .tcmp stream and check its CRC, discarding the output.
 *
 * Exactly the checks of tcomp_decompress_stream(), with nothing written.
 *
 * @param in Open stream, read in binary mode. Must not be NULL.
 * @return As tcomp_decompress_stream().
 */
tcomp_status tcomp_verify_stream(FILE *in);

/**
 * @brief Read a .tcmp stream's header and trailer without decoding the payload.
 *
 * Fast: the payload is read (to find the trailer and the file size) but not
 * decoded, so a damaged payload is not detected. Use tcomp_verify_stream()
 * for that. A STORE file whose length disagrees with its header is reported.
 *
 * @param in   Open stream, read in binary mode. Must not be NULL.
 * @param info Filled in on success. Must not be NULL.
 * @return TCOMP_OK, or one of TCOMP_ERR_INVALID_ARG, TCOMP_ERR_IO,
 *         TCOMP_ERR_NOMEM, TCOMP_ERR_BAD_MAGIC, TCOMP_ERR_BAD_VERSION,
 *         TCOMP_ERR_BAD_METHOD, TCOMP_ERR_TRUNCATED, TCOMP_ERR_CORRUPT.
 */
tcomp_status tcomp_read_info(FILE *in, tcomp_info *info);

#endif /* TCOMP_CONTAINER_H */
