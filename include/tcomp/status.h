/**
 * @file status.h
 * @brief Status codes returned by every fallible tcomp function.
 *
 * Library code never calls exit() or prints to stderr. It returns a
 * tcomp_status and lets the caller (usually src/main.c) decide what to do.
 * See docs/decisions/0002-error-handling.md for the reasoning.
 */
#ifndef TCOMP_STATUS_H
#define TCOMP_STATUS_H

#define TCOMP_VERSION_STRING "0.3.0"

typedef enum {
    TCOMP_OK = 0,          /**< Success. Always zero so `if (st)` means "failed". */
    TCOMP_ERR_IO,          /**< A read or write on a stream failed. */
    TCOMP_ERR_NOMEM,       /**< An allocation failed. */
    TCOMP_ERR_INVALID_ARG, /**< A caller passed a NULL or out-of-range argument. */
    TCOMP_ERR_BAD_MAGIC,   /**< Input does not start with the "TCMP" magic bytes. */
    TCOMP_ERR_BAD_VERSION, /**< Format version is newer than this build understands. */
    TCOMP_ERR_BAD_METHOD,  /**< Compression method id is unknown. */
    TCOMP_ERR_TRUNCATED,   /**< Input ended before a complete structure was read. */
    TCOMP_ERR_CORRUPT,     /**< Input is structurally invalid (e.g. impossible code lengths). */
    TCOMP_STATUS_COUNT     /**< Number of codes above. Not a real status. */
} tcomp_status;

/**
 * @brief Human-readable message for a status code.
 *
 * @param status Any value, including ones outside the enum.
 * @return A static string. Never NULL. The caller must not free it.
 */
const char *tcomp_strerror(tcomp_status status);

#endif /* TCOMP_STATUS_H */
