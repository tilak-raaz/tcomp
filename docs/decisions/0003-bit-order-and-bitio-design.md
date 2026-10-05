# 0003: MSB-first bit order; memory-buffer bit I/O

- **Date:** 2026-10-06
- **Milestone:** M1
- **Status:** Accepted

## Context

Huffman codes and LZ77 fields are not whole bytes, so they must be packed into bytes. Two choices shape everything built on top: the order bits fill a byte, and whether the bit writer talks to a file or to memory.

## Options considered

**Bit order**

1. **LSB-first** (DEFLATE, gzip, zlib). The first bit goes in bit 0 (0x01) of the first byte. Fast with a 64-bit accumulator, but Huffman codes must be bit-reversed before writing, which is a common source of confusion.
2. **MSB-first** (JPEG, bzip2). The first bit goes in bit 7 (0x80). Bytes read left to right in a hex dump match the bit sequence, and a canonical Huffman code is written exactly as it is computed. Table-driven decoding works directly: peeking the next N bits gives the table index.

**Where bits go**

1. **Straight to a `FILE *`.** Simple, but ties encoding to I/O.
2. **A growable memory buffer.** A block can be encoded in memory, measured, and written later. Required for blocks (M7) and for threads that encode in parallel (M9).

## Decision

MSB-first, with a writer that owns a growable memory buffer and a reader that borrows a caller's buffer. Fields are 0 to 32 bits per call. The writer keeps pending bits in a 64-bit accumulator: fewer than 8 pending bits plus up to 32 new ones never exceeds 39. The reader loads a 5-byte (40-bit) window for every peek, which covers any 32-bit field at any bit offset.

Further choices:

- **Reader peek pads with zeros past the end; read and skip do not.** A Huffman decoder can always peek a full table index near the end of the data, then `skip` reports truncation if the code it found is longer than the bits really left.
- **Writing a value wider than its field is an error, not masked.** Masking would hide caller bugs as silent corruption.
- **Errors leave state unchanged.** A failed write writes nothing; a failed read does not move the position. Callers can report an error without wondering what half-happened.

## Consequences

- Hex dumps of `.tcmp` payloads are readable by hand, which helps debugging M2 and M4.
- tcomp's bitstream is not compatible with DEFLATE's. That was never a goal.
- The writer appends one byte at a time. That is simple and correct; M6 profiling will show whether a faster path (writing 8 bytes at once) is worth it.
