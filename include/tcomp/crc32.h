/**
 * @file crc32.h
 * @brief CRC-32 checksums, as used by gzip, zlib, PNG and Ethernet.
 *
 * Reflected polynomial 0xEDB88320, initial value and final XOR 0xFFFFFFFF
 * (both handled inside, so callers start from 0). "123456789" gives
 * 0xCBF43926, and results match zlib's crc32() and Python's zlib.crc32().
 *
 * The implementation is slicing-by-8: eight bytes per step through eight
 * 256-entry tables. See docs/decisions/0008-format-v1-and-crc32.md.
 */
#ifndef TCOMP_CRC32_H
#define TCOMP_CRC32_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Extend a CRC-32 with more data.
 *
 * Start with crc = 0. Feeding data in pieces gives the same result as one
 * call over all of it:
 *
 *   uint32_t crc = 0;
 *   crc = tcomp_crc32_update(crc, part1, len1);
 *   crc = tcomp_crc32_update(crc, part2, len2);
 *
 * @param crc  CRC of the data so far (0 for none).
 * @param data Bytes to add. May be NULL only if len is 0.
 * @param len  Number of bytes.
 * @return     CRC of the data so far followed by @p data.
 */
uint32_t tcomp_crc32_update(uint32_t crc, const void *data, size_t len);

#endif /* TCOMP_CRC32_H */
