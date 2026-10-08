# Design

This document explains how tcomp is put together and the conventions every module follows. It grows with each milestone.

## Architecture (M3)

```
             src/main.c  (CLI: argument parsing, -m METHOD, exit codes, messages)
                  │
                  ▼
        ┌────────────────────────────────────────────┐
        │ libtcomp.a                                  │
        │                                             │
        │  container.c   header, method choice (AUTO), │
        │       │        STORE, HUFFMAN, LZ77 payloads │
        │       ├──► huffman.c   lengths, canonical    │
        │       │                codes, decode table   │
        │       ├──► lz77.c      tokens, naive matcher,│
        │       │                expansion             │
        │       └──► bitio.c     bit writer/reader     │
        │  status.c      error messages                │
        └────────────────────────────────────────────┘
```

The CLI and the unit tests both link against the same static library, so tests exercise exactly the code the CLI ships.

**Data flow, HUFFMAN compression:** read all input → count byte frequencies → `tcomp_huff_build_lengths` → `tcomp_huff_assign_codes` → compute the exact output size; if it is not smaller than the input, write STORE instead → otherwise write header, size, 4-bit lengths and one code per byte through the bit writer.

**Data flow, LZ77 compression:** read all input → `tcomp_lz77_parse` into a token array → write each token as a 9-bit literal or 24-bit match → header, size, bitstream.

**Decompression:** read header → validate → dispatch on method. HUFFMAN: read size and lengths → validate → build the decode table → decode *n* symbols into a 64 KB output buffer → check padding. LZ77: read size → bound it by the bits available → decode tokens into a 96 KB sliding buffer that always keeps the last 32 KB → check padding.

Planned modules, each added in its milestone: `crc32.c` (M5), `matcher.c` (M6), `block.c` (M7), `threads.c` (M9).

## Modules

### bitio (`include/tcomp/bitio.h`, M1)

Packs variable-width fields (0–32 bits) into bytes and unpacks them. Bits are MSB-first: the first bit written is bit 7 (0x80) of the first byte. See [decision 0003](decisions/0003-bit-order-and-bitio-design.md).

| Type | Owns memory? | Key operations |
| --- | --- | --- |
| `tcomp_bitwriter` | Yes, a growable buffer; free with `tcomp_bw_free()` | `write_bits`, `write_bit`, `flush` (pad to byte), `data`/`size` |
| `tcomp_bitreader` | No, borrows the caller's buffer | `read_bits`, `read_bit`, `peek_bits`, `skip_bits`, `align` |

How the writer works: pending bits sit right-aligned in a 64-bit accumulator. Each write shifts the accumulator left by the field width and ORs the value in, then moves every complete byte (top 8 pending bits) into the buffer. Fewer than 8 bits remain pending between calls.

How the reader works: to peek N bits at bit position `pos`, it loads the 5 bytes starting at `pos / 8` into a 40-bit number (bytes past the end count as 0), shifts right so the wanted field sits at the bottom, and masks it. Five bytes cover the worst case: a 32-bit field starting 7 bits into a byte.

### huffman (`include/tcomp/huffman.h`, M2)

Length-limited canonical Huffman coding for any alphabet up to 1024 symbols (M4 reuses it for LZ77's two alphabets). See [decision 0004](decisions/0004-huffman-design.md).

| Function | Input → output | How |
| --- | --- | --- |
| `tcomp_huff_build_lengths` | frequencies → code lengths ≤ limit | Min-heap merges the two lightest nodes until one tree remains; leaf depth = code length. If too deep, halve all frequencies (rounding up) and rebuild. |
| `tcomp_huff_assign_codes` | lengths → canonical codes | RFC 1951 3.2.2: count codes per length, compute the first code of each length, hand out codes in symbol order. Rejects lengths that break the Kraft inequality. |
| `tcomp_huff_decoder_init` | lengths → lookup table | Table of 2^maxlen entries. A code of length L fills the 2^(maxlen−L) entries that begin with it, each holding `symbol << 4 \| L`. |
| `tcomp_huff_decode_symbol` | bit reader → symbol | Peek maxlen bits, look up the entry, skip only L bits. Empty entry → corrupt; L past the end → truncated. |

Why one table lookup is enough: in a prefix code no code is the start of another, so whatever bits follow a code, the first maxlen bits identify it uniquely.

### lz77 (`include/tcomp/lz77.h`, M3)

Turns bytes into literal/match tokens and back, with DEFLATE's parameters (matches 3–258 bytes, window 32 KB). Knows nothing about bits; the container decides how tokens are encoded. See [decision 0005](decisions/0005-lz77-design.md).

| Function | Does |
| --- | --- |
| `tcomp_lz77_parse` | Greedy parse with a naive matcher: at each position try every distance from 1 to the window, keep the longest match (nearest on ties), emit it if ≥ 3 bytes, else emit a literal. O(n × window). |
| `tcomp_lz77_expand` | Tokens → bytes in a caller buffer, validating each token. Matches copy byte by byte so overlapping copies (distance < length) work. |
| `tcomp_lz77_tokens_*` | A growable token array (4 bytes per token). |

The matcher compares against the *input*, even where a match overlaps the bytes it is about to produce. That is correct because the decoder will have produced exactly those bytes, one at a time, by the time it reads them.

The streaming decoder in `container.c` does not use `tcomp_lz77_expand`: it never holds more than 96 KB of output. When a token would not fit, it writes out everything not yet written and slides the newest 32 KB (all a match can reach) to the front of the buffer.

## Conventions

**Errors.** Every function that can fail returns `tcomp_status` (`include/tcomp/status.h`). `TCOMP_OK` is zero. Library code never prints, never calls `exit()`, and never aborts on bad input. Only `src/main.c` turns a status into a message and an exit code. See [decision 0002](decisions/0002-error-handling.md).

**Streams, not whole files.** The library API takes `FILE *` streams and reads until EOF. This keeps stdin/stdout support free and prepares for streaming in M7.

**Memory ownership.** Each header comment says who owns any pointer passed in or returned. Callers free what they allocate; the library frees what it allocates.

**Untrusted input.** The decompressor treats its input as hostile: every length, index and table read from a file is validated before use. This is checked by negative tests now and fuzzing from M8.

**Headers.** Public headers live in `include/tcomp/` and are included as `"tcomp/name.h"`. Internal helpers are `static`; `-Wmissing-prototypes` enforces this.

## Testing strategy

| Layer | Where | What it proves |
| --- | --- | --- |
| Unit tests | `tests/unit/test_<module>.c` | Each module's functions in isolation, including error paths; exact expected bytes; seeded random roundtrips |
| Roundtrip tests | `tests/roundtrip.sh` | `decompress(compress(x)) == x` through the real CLI, via files and pipes, on edge-case data |
| Negative tests | `tests/roundtrip.sh`, `test_container.c` | Bad input exits with code 1, leaves no output file, triggers no sanitizer; hand-built hostile files hit every decoder check |
| Corpus | `make corpus` (`tests/corpus.sh`) | Every Canterbury corpus file round-trips with every method; prints ratios |
| Fuzzing | `fuzz/` (M8) | The decoder survives arbitrary bytes |

Debug builds run all tests under AddressSanitizer and UndefinedBehaviorSanitizer. Sanitizer failures exit with code 86 so they are never confused with a normal error.
