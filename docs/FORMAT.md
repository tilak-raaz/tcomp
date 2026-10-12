# The .tcmp file format

This document specifies every byte of a `.tcmp` file. It should be precise enough to write a compatible decoder without reading tcomp's source code.

Conventions: offsets and sizes are in bytes. Multi-byte integers are **little-endian**. Bit-packed data is **MSB-first**: the first bit of the stream is the most significant bit (0x80) of the first byte, and each multi-bit field is stored from its most significant bit down. A bitstream ends with 0 bits padding it to a whole byte.

## Version 0 (current, M0–M5)

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
| 2 | LZ77 | M3 | LZ77 tokens in fixed-width fields (below) |
| 3 | LZH | M5 | LZ77 tokens Huffman-coded, DEFLATE-style (below) |

All other method ids are reserved. The CLI's `-m auto` is not a method id: it chooses whichever method gives the smallest file.

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

### LZ77 payload (method 2)

| Offset in payload | Size | Field |
| --- | --- | --- |
| 0 | 8 | Original size *n* in bytes, unsigned little-endian |
| 8 | rest of file | Bitstream |

The bitstream is a sequence of tokens, then 0–7 zero bits of padding. Each token starts with a 1-bit flag:

| Flag | Token | Following fields | Total bits |
| --- | --- | --- | --- |
| `0` | Literal | 8 bits: the byte value | 9 |
| `1` | Match | 8 bits: *length* − 3; 15 bits: *distance* − 1 | 24 |

A literal appends one byte to the output. A match appends *length* (3–258) bytes copied from *distance* (1–32768) bytes before the current end of the output, **one byte at a time**: when *distance* < *length* the copy reads bytes it has just written, which repeats the last *distance* bytes.

Example: the tokens `0 'a'`, `0 'b'`, `1 (length 7, distance 2)` decode to `ababababa`.

Tokens continue until exactly *n* bytes have been produced.

### LZH payload (method 3)

| Offset in payload | Size | Field |
| --- | --- | --- |
| 0 | 8 | Original size *n* in bytes, unsigned little-endian |
| 8 | rest of file | Bitstream |

The bitstream has the structure of an RFC 1951 dynamic-Huffman block, written MSB-first. Tokens have the same meaning as in the LZ77 method.

**1. Counts**

| Bits | Field | Meaning |
| --- | --- | --- |
| 5 | HLIT | Literal/length code lengths sent, minus 257 (valid: 0–29, so 257–286) |
| 5 | HDIST | Distance code lengths sent, minus 1 (valid: 0–29, so 1–30) |
| 4 | HCLEN | Code-length code lengths sent, minus 4 (4–19) |

**2. Code-length code.** HCLEN + 4 fields of 3 bits: the code lengths (0–7) of code-length symbols in the order `16 17 18 0 8 7 9 6 10 5 11 4 12 3 13 2 14 1 15`. Symbols not sent have length 0. Codes are assigned canonically, as for HUFFMAN.

**3. Code lengths.** HLIT + HDIST lengths, literal/length table first, then distance table, as one sequence of code-length symbols (a run may cross from one table to the other). Each symbol is followed by its extra bits:

| Symbol | Extra bits | Meaning |
| --- | --- | --- |
| 0–15 | 0 | This code length |
| 16 | 2 | Repeat the previous length 3 + extra times (3–6) |
| 17 | 3 | 3 + extra zeros (3–10) |
| 18 | 7 | 11 + extra zeros (11–138) |

Lengths beyond those sent are 0. Both codes are then assigned canonically. Maximum code length: 15.

**4. Tokens.** Each token starts with a literal/length code:

| Symbol | Meaning |
| --- | --- |
| 0–255 | Literal byte |
| 256 | Reserved, invalid |
| 257–285 | Match: length = base + extra bits (table below), then a distance code |

Length symbols (base length, extra bits): 257–264 → 3–10 (0); 265–268 → 11, 13, 15, 17 (1); 269–272 → 19, 23, 27, 31 (2); 273–276 → 35, 43, 51, 59 (3); 277–280 → 67, 83, 99, 115 (4); 281–284 → 131, 163, 195, 227 (5); 285 → 258 (0).

Distance symbols (base distance, extra bits): 0–3 → 1–4 (0); then pairs of symbols per extra-bit count *k* = 1…13, with bases 5, 7 (1); 9, 13 (2); 17, 25 (3); 33, 49 (4); 65, 97 (5); 129, 193 (6); 257, 385 (7); 513, 769 (8); 1025, 1537 (9); 2049, 3073 (10); 4097, 6145 (11); 8193, 12289 (12); 16385, 24577 (13). Symbols 28–29 cover 16385–32768.

Extra bits are an unsigned number of the stated width, MSB-first, added to the base. Example: length 13 is symbol 266 followed by extra bit `0`; distance 6 is symbol 4 followed by extra bit `1`.

**5. Padding:** 0–7 zero bits.

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

For LZ77, after rule 4:

5. The size field is present → otherwise *truncated*.
6. *n* ≤ ⌊bits remaining / 9⌋ × 258, since every token is at least 9 bits and produces at most 258 bytes → otherwise *truncated*. Checked before any output is written.
7. Each match's *distance* ≤ bytes produced so far → otherwise *corrupt*.
8. Each token's length ≤ *n* − bytes produced so far → otherwise *corrupt*.
9. The data does not end inside a token → otherwise *truncated*.
10. After *n* bytes, fewer than 8 bits remain and all of them are 0 → otherwise *corrupt*.

For LZH, after rule 4:

5. The size field and all header fields are present → otherwise *truncated*.
6. HLIT ≤ 286 and HDIST ≤ 30 → otherwise *corrupt*.
7. The code-length code, and the literal/length and distance codes, satisfy the Kraft inequality → otherwise *corrupt*.
8. Symbol 16 does not come first; no run goes past HLIT + HDIST lengths → otherwise *corrupt*.
9. *n* ≤ bits remaining × 258, since every token is at least 1 bit and produces at most 258 bytes → otherwise *truncated*. Checked before any output is written.
10. Every code read matches a symbol, and symbol 256 never appears → otherwise *corrupt*.
11. Rules 7–10 of LZ77 (distance within output, length within *n*, no truncated token, zero padding).

### Known limitations of version 0

- No checksum. A STORE file truncated inside its payload cannot be detected, and some single-bit corruptions of HUFFMAN, LZ77 or LZH data decode to wrong output without error. Version 1 (M6) adds a CRC32.
- HUFFMAN, LZ77 and LZH compression read the whole input into memory, and LZH uses one set of tables for the whole file. Blocks (M7) remove both limits.
- LZ77's fixed-width fields are deliberately simple; LZH (method 3) replaces them with Huffman codes.

## Planned changes

| Version | Milestone | Adds |
| --- | --- | --- |
| 1 | M6 | Original size and CRC32 in the header, LZ77+Huffman method, `info` command |
| 1 + blocks | M7 | Block structure for streaming and parallelism |
