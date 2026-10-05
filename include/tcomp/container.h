/**
 * @file container.h
 * @brief Reading and writing the .tcmp container around compressed data.
 *
 * In M0 the only method is STORE (bytes are copied unchanged), so the full
 * pipeline (CLI, file handling, tests, CI) can be exercised before any real
 * compression exists. Later milestones add methods without changing this API.
 *
 * The exact byte layout is specified in docs/FORMAT.md.
 */
#ifndef TCOMP_CONTAINER_H
#define TCOMP_CONTAINER_H

#include <stdio.h>

#include "tcomp/status.h"

/** Compression methods. The numeric values are part of the file format. */
typedef enum {
    TCOMP_METHOD_STORE = 0 /**< No compression; payload is the raw input. */
} tcomp_method;

/**
 * @brief Compress everything readable from @p in and write a .tcmp stream to @p out.
 *
 * Reads until EOF. Does not close either stream.
 *
 * @param in  Open stream, read in binary mode. Must not be NULL.
 * @param out Open stream, written in binary mode. Must not be NULL.
 * @return TCOMP_OK, TCOMP_ERR_INVALID_ARG or TCOMP_ERR_IO.
 */
tcomp_status tcomp_compress_stream(FILE *in, FILE *out);

/**
 * @brief Read a .tcmp stream from @p in and write the original bytes to @p out.
 *
 * Validates the header before writing anything. Does not close either stream.
 * On error, @p out may already hold partial output; the caller should discard it.
 *
 * @param in  Open stream, read in binary mode. Must not be NULL.
 * @param out Open stream, written in binary mode. Must not be NULL.
 * @return TCOMP_OK, or one of TCOMP_ERR_INVALID_ARG, TCOMP_ERR_IO,
 *         TCOMP_ERR_BAD_MAGIC, TCOMP_ERR_BAD_VERSION, TCOMP_ERR_BAD_METHOD,
 *         TCOMP_ERR_TRUNCATED.
 */
tcomp_status tcomp_decompress_stream(FILE *in, FILE *out);

#endif /* TCOMP_CONTAINER_H */
