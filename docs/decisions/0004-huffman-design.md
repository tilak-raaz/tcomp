# 0004: Canonical Huffman, 15-bit limit, table-driven decoding

- **Date:** 2026-10-06
- **Milestone:** M2
- **Status:** Accepted

## Context

M2 adds the first real compression: order-0 Huffman coding of bytes. Four choices shape it: how the code is sent to the decoder, how long codes may get, how decoding works, and what happens when compression does not help.

## Options considered

**Sending the code**

1. **Serialize the tree.** Works, but the format depends on tree shape, and a corrupt tree is awkward to validate.
2. **Send code lengths only; both sides derive canonical codes.** One number per symbol. Validation is a single Kraft-inequality check. Used by DEFLATE, zstd, bzip2.

**Limiting code length**

1. **No limit.** Optimal, but a skewed input can need a code longer than the decoder table can index (Fibonacci-like frequencies over 256 symbols can need 255 bits).
2. **Package-merge.** Optimal under a length limit, but significantly more complex.
3. **Flatten and retry.** If the tree is too deep, halve every frequency (rounding up) and rebuild. Slightly suboptimal in rare cases, simple to verify, always terminates.

**Decoding**

1. **Walk the tree bit by bit.** Simple but slow: one branch per bit.
2. **Single lookup table indexed by the next N bits**, where N is the longest code. One lookup per symbol. Table size is 2^N entries, at most 2^15 × 2 bytes = 64 KB.

## Decision

- Store **4-bit code lengths** for all 256 byte values (128 bytes); codes are rebuilt canonically, as in RFC 1951.
- Limit codes to **15 bits** (same as DEFLATE) using **flatten-and-retry**. On the Canterbury corpus the limit never triggers.
- Decode with a **single-level table** built from the lengths; each entry packs `symbol << 4 | length`. The bit reader's zero-padded peek (decision 0003) lets the last symbols use the same table.
- Break ties by node index so the same input always produces identical output.
- **AUTO method:** compute the exact Huffman size from the frequencies before encoding, and store the data raw if Huffman would not be strictly smaller.

## Consequences

- On Canterbury text files Huffman lands within 0.6–1.6% of the order-0 entropy bound. Small files lose ~6% to the fixed 142 bytes of overhead (6-byte header, 8-byte size, 128-byte length table); `ptt5`, where one byte value dominates, is 37% above the bound because Huffman cannot spend less than 1 bit per symbol. LZ77 (M3) and arithmetic coding/ANS (M10+) attack that limit.
- The decoder rejects impossible length tables (Kraft violation), codes that map to no symbol, size fields larger than the data could hold, and non-zero padding or trailing bytes.
- A corrupted size field can still go undetected if it changes the count by a symbol that fits in the padding bits. The CRC32 in format version 1 (M5) closes this gap.
- Compression reads the whole input into memory. Blocks (M7) remove this limit.
