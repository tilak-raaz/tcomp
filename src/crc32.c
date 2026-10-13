#include "tcomp/crc32.h"

#include "crc32_table.h"

/* Slicing-by-8. The classic table method handles one byte per step:
 *
 *     crc = TABLE[0][(crc ^ byte) & 0xff] ^ (crc >> 8)
 *
 * and each step depends on the one before, so a CPU can't overlap them.
 * Slicing-by-8 XORs the next 4 input bytes into the CRC, then looks up
 * all 8 bytes at once: TABLE[k][b] is the effect of byte b followed by k
 * zero bytes. The 8 lookups are independent, so they run in parallel.
 *
 * Bytes are assembled into words explicitly, so the code gives the same
 * result on little- and big-endian machines and needs no aligned loads
 * (compilers turn it into one load on x86 and ARM). */
uint32_t tcomp_crc32_update(uint32_t crc, const void *data, size_t len) {
    const uint8_t *p = data;
    crc = ~crc;
    while (len >= 8) {
        uint32_t lo = crc ^ ((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                             (uint32_t)p[3] << 24);
        uint32_t hi =
            (uint32_t)p[4] | (uint32_t)p[5] << 8 | (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24;
        crc = CRC32_TABLE[7][lo & 0xff] ^ CRC32_TABLE[6][(lo >> 8) & 0xff] ^
              CRC32_TABLE[5][(lo >> 16) & 0xff] ^ CRC32_TABLE[4][lo >> 24] ^
              CRC32_TABLE[3][hi & 0xff] ^ CRC32_TABLE[2][(hi >> 8) & 0xff] ^
              CRC32_TABLE[1][(hi >> 16) & 0xff] ^ CRC32_TABLE[0][hi >> 24];
        p += 8;
        len -= 8;
    }
    while (len-- > 0) {
        crc = CRC32_TABLE[0][(crc ^ *p++) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}
