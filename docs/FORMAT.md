# The .tcmp file format

This document specifies every byte of a `.tcmp` file. It should be precise enough to write a compatible decoder without reading tcomp's source code.

Conventions: offsets and sizes are in bytes. Multi-byte integers are **little-endian**. Bit-packed data is **MSB-first**: the first bit of the stream is the most significant bit (0x80) of the first byte, and each multi-bit field is stored from its most significant bit down. A bitstream ends with 0 bits padding it to a whole byte.

## Version 0 (current, M0–M2)

Version 0 is a minimal container used while the real codec is built.

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0 | 4 | Magic | ASCII `TCMP` (`54 43 4D 50`) |
| 4 | 1 | Format version | `0` |
| 5 | 1 | Method | See below |
| 6 | rest of file | Payload | Depends on method |

### Methods

| Id | Name | Since | Payload |
| --- | --- | --- | --- |
| 0 | STORE | M0 | The original data, unchanged, up to end of file |
| 1 | HUFFMAN | M2 | Order-0 canonical Huffman coding of bytes (below) |

All other method ids are reserved. The CLI's `-m auto` is not a method id: it chooses STORE or HUFFMAN, whichever is smaller.

### HUFFMAN payload (method 1)

| Offset in payload | Size | Field |
| --- | --- | --- |
| 0 | 8 | Original size *n* in bytes, unsigned little-endian |
| 8 | rest of file | Bitstream |

The bitstream contains, in order:

1. **Code lengths:** 256 fields of 4 bits, for byte values 0 to 255 in order. A length of 0 means the byte value does not occur; 1–15 is its code length in bits. (128 bytes in total.)
2. **Data:** *n* Huffman codes, one per original byte, in order.
3. **Padding:** 0–7 zero bits to finish the last byte. Nothing follows.

**Canonical codes.** Codes are not stored; they are derived from the lengths as in RFC 1951, section 3.2.2:

1. Count how many symbols have each length 1–15.
2. The first code of length L is `(first_code(L-1) + count(L-1)) << 1`, starting from `first_code(1) = 0` with `count(0) = 0`.
3. Walk byte values 0 to 255 in order; each symbol with length L > 0 takes the next unused code of length L.

Example: lengths `A=2, B=1, C=3, D=3` give codes `B=0, A=10, C=110, D=111`.

### Decoder rules

A decoder must check, in this order:

1. The bytes present match the magic. Fewer than 4 bytes that do not match the start of `TCMP` → *bad magic*.
2. At least 6 header bytes exist → otherwise *truncated*.
3. Format version ≤ the highest version the decoder supports → otherwise *unsupported version*.
4. Method id is known → otherwise *unknown method*.

For HUFFMAN, additionally:

5. The size field and all 256 code lengths are present → otherwise *truncated*.
6. The lengths satisfy the Kraft inequality, Σ 2^-length ≤ 1 over used symbols → otherwise *corrupt*. (An incomplete code, e.g. a single symbol of length 1, is valid.)
7. *n* is at most the number of bits remaining after the lengths, since every code is at least 1 bit → otherwise *truncated*. This check happens before any output is written.
8. Every code read matches a symbol → otherwise *corrupt*; the data does not end inside a code → otherwise *truncated*.
9. After *n* codes, fewer than 8 bits remain and all of them are 0 → otherwise *corrupt*.

### Known limitations of version 0

- No checksum. A STORE file truncated inside its payload cannot be detected, and some single-bit corruptions of HUFFMAN data decode to wrong output without error. Version 1 (M5) adds a CRC32.
- HUFFMAN compression reads the whole input into memory. Blocks (M7) remove this.

## Planned changes

| Version | Milestone | Adds |
| --- | --- | --- |
| 1 | M5 | Original size and CRC32 in the header, LZ77+Huffman method, `info` command |
| 1 + blocks | M7 | Block structure for streaming and parallelism |
