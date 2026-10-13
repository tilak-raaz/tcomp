# Design

This document explains how tcomp is put together and the conventions every module follows. It grows with each milestone.

## Architecture (M6)

```
             src/main.c  (CLI: compress, decompress, info, verify; exit codes, messages)
                  │
                  ▼
        ┌──────────────────────────────────────────────┐
        │ libtcomp.a                                    │
        │                                               │
        │  container.c   header + trailer, method       │
        │       │        choice (AUTO), payloads, token  │
        │       │        expansion, CRC check            │
        │       ├──► lzh.c       DEFLATE-style codes     │
        │       │                for LZ77 tokens         │
        │       ├──► huffman.c   lengths, canonical      │
        │       │                codes, decode table     │
        │       ├──► lz77.c      tokens, hash-chain      │
        │       │                matcher, expansion      │
        │       ├──► bitio.c     bit writer/reader       │
        │       └──► crc32.c     CRC-32, slicing-by-8    │
        │  status.c      error messages                  │
        └──────────────────────────────────────────────┘
```

The CLI, the unit tests and the benchmark (`bench/bench.c`) all link against the same static library, so tests and benchmarks exercise exactly the code the CLI ships.

**Every file** is a 15-byte header (magic, version 1, method, flags, original size), the method's payload, and a 4-byte trailer holding the CRC-32 of the original data ([FORMAT.md](FORMAT.md), [decision 0008](decisions/0008-format-v1-and-crc32.md)). The CRC is computed over the input during compression and over the output during decompression, so it checks the whole pipeline.

**Data flow, AUTO (the default):** read all input → plan HUFFMAN (count frequencies, build codes, compute exact size) → parse LZ77 tokens once → compute the LZ77 size → plan LZH on the same tokens → encode only whichever of STORE, HUFFMAN, LZ77 and LZH is smallest → write header, payload, CRC trailer. Nothing is encoded twice.

**Data flow, LZH compression:** read all input → `tcomp_lz77_parse` → `tcomp_lzh_plan` (count literal/length and distance symbols, build both codes, run-length code their lengths, build the code-length code, total the exact size) → `tcomp_lzh_write` → header, bitstream, trailer.

**Data flow, HUFFMAN compression:** read all input → count byte frequencies → `tcomp_huff_build_lengths` → `tcomp_huff_assign_codes` → compute the exact output size; write 4-bit lengths and one code per byte through the bit writer → header, bitstream, trailer.

**Data flow, LZ77 compression:** read all input → `tcomp_lz77_parse` (hash chains, 128 candidates per position) into a token array → write each token as a 9-bit literal or 24-bit match → header, bitstream, trailer.

**Decompression:** read the 15-byte header → validate magic, version, method, flags → dispatch on method. STORE: copy exactly *n* bytes in 64 KB chunks, then read the trailer and require end of file. Other methods: read the rest of the file; the last 4 bytes are the trailer, the rest the bitstream. HUFFMAN: read lengths → validate → build the decode table → decode *n* symbols into a 64 KB output buffer → check padding. LZ77 and LZH: (LZH: read and validate the code tables) → bound the size by the bits available → read tokens with the method's token reader and expand them into a 96 KB sliding buffer that always keeps the last 32 KB → check padding. The expansion loop (`expand_tokens`) is shared; the methods differ only in how a token is read. Finally compare the CRC of everything written with the trailer.

**The output sink.** Decoders never write to the `FILE *` directly. They hand each chunk to `sink_write`, which adds it to a running CRC-32 and then writes it, or, for `tcomp verify`, discards it. So the CRC can't be skipped by a code path that forgets it, and `verify` runs exactly the same checks as `decompress`.

**`info` and `verify`.** `tcomp_read_info` validates the header, reads the rest of the file in 64 KB chunks keeping only the last 4 bytes, and reports method, sizes and the stored CRC, without decoding. `tcomp_verify_stream` is decompression into a discarding sink.

Planned modules, each added in its milestone: `block.c` (M7), `threads.c` (M9).

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

Length-limited canonical Huffman coding for any alphabet up to 1024 symbols (M5 reuses it for LZ77's two alphabets). See [decision 0004](decisions/0004-huffman-design.md).

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
| `tcomp_lz77_parse` | Greedy parse with hash chains: at each position, try only earlier positions whose next 3 bytes hash the same, nearest first, up to `max_chain` of them; keep the longest match (nearest on ties), emit it if ≥ 3 bytes, else a literal. See [decision 0006](decisions/0006-hash-chain-matcher.md). |
| `tcomp_lz77_parse_naive` | Same rules, but tries every distance from 1 to the window: O(n × window). The reference for tests and benchmarks. |
| `tcomp_lz77_expand` | Tokens → bytes in a caller buffer, validating each token. Matches copy byte by byte so overlapping copies (distance < length) work. |
| `tcomp_lz77_tokens_*` | A growable token array (4 bytes per token). |

How the hash chains work: `head[h]` holds the most recent position whose next 3 bytes hash to `h`; `prev[p % 32768]` holds the position before `p` with the same hash. After each token, every position it covered is pushed onto its chain. To search at position `i`, start at `head[hash(i)]` and follow `prev` links, stopping at the window edge or after `max_chain` steps. Because any match of 3 or more bytes shares the first 3 bytes, it shares the hash, so no useful candidate is missed; with no limit the tokens equal the naive parser's exactly.

The matchers compare against the *input*, even where a match overlaps the bytes it is about to produce. That is correct because the decoder will have produced exactly those bytes, one at a time, by the time it reads them.

The streaming decoder in `container.c` does not use `tcomp_lz77_expand`: it never holds more than 96 KB of output. When a token would not fit, it writes out everything not yet written and slides the newest 32 KB (all a match can reach) to the front of the buffer.

### lzh (`include/tcomp/lzh.h`, M5)

Huffman-codes LZ77 tokens with DEFLATE's layout: a 286-symbol literal/length code and a 30-symbol distance code, extra bits for exact lengths and distances, and both tables' code lengths run-length coded and Huffman-coded with a 19-symbol code-length code. See [decision 0007](decisions/0007-lzh-design.md) and the byte-level spec in [FORMAT.md](FORMAT.md).

| Function | Does |
| --- | --- |
| `tcomp_lzh_length_code`, `tcomp_lzh_distance_code` | Value → (symbol, extra bits, extra value), by binary search in RFC 1951's base tables. |
| `tcomp_lzh_plan` | Tokens → both Huffman codes, the run-length-coded tables, the code-length code, and the exact bitstream size. Writes nothing. |
| `tcomp_lzh_write` | Header (counts, code-length code, tables) then every token's codes and extra bits. |
| `tcomp_lzh_read_header` | Reads and validates the tables; builds the two decoders. |
| `tcomp_lzh_read_token` | One literal/length symbol (+ extra bits, + distance symbol and extra bits for a match). |

Why the code-length code is sent in a strange order (16, 17, 18, 0, 8, 7, …): the symbols most likely to be used come first, so the encoder can stop after the last non-zero one and drop the rest (HCLEN).

### crc32 (`include/tcomp/crc32.h`, M6)

One function, `tcomp_crc32_update(crc, data, len)`: start from 0, feed data in any number of pieces, get the same result as one call. The variant is gzip's and zlib's (reflected polynomial 0xEDB88320), so results can be checked against `python3 -c "import zlib; print(hex(zlib.crc32(b'...')))"`. See [decision 0008](decisions/0008-format-v1-and-crc32.md).

How slicing-by-8 works: the classic table method handles one byte per step, `crc = T0[(crc ^ byte) & 0xff] ^ (crc >> 8)`, and every step waits for the one before. Slicing-by-8 takes 8 input bytes per step: it XORs the CRC into the first 4, then looks up all 8 bytes at once in 8 tables, where `Tk[b]` is the CRC effect of byte `b` followed by `k` zero bytes. The 8 lookups don't depend on each other, so the CPU runs them in parallel: 2.1 GB/s against 387 MB/s for one byte at a time. Leftover bytes (fewer than 8) take the classic path.

The 8 KB of tables are generated by `tools/gen-crc32-table.py` into `src/crc32_table.h` and committed, so nothing is computed at start-up and there is no initialisation to make thread-safe. The unit tests check every result against a bit-at-a-time implementation written from the definition.

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
| Exhaustive corruption | `test_container.c` | For every method, every single-bit flip and every truncation of a compressed file is rejected or decodes to the exact original: never to wrong data |
| Golden files | `tests/fixtures/v1/`, `tests/roundtrip.sh` | Files written by tcomp 1.0 still decode, and the encoder still produces them byte for byte. If an encoder change is intended, regenerate them; the decode check must always pass |
| Differential | `test_lz77.c`, `test_crc32.c` | Hash chains with no limit give exactly the naive parser's tokens; slicing-by-8 CRC gives exactly the bit-at-a-time CRC, at every length and alignment |
| Corpus | `make corpus` (`tests/corpus.sh`) | Every Canterbury corpus file round-trips with every method; prints ratios |
| Benchmark | `make bench` (`bench/bench.c`) | In-process timing, best of 5: matcher sweep against naive, and every method end to end with output verified |
| Fuzzing | `fuzz/` (M8) | The decoder survives arbitrary bytes |

Debug builds run all tests under AddressSanitizer and UndefinedBehaviorSanitizer. Sanitizer failures exit with code 86 so they are never confused with a normal error.

**Mutation testing.** At each milestone, small bugs are planted in the new code one at a time (an off-by-one, a skipped check, a swapped table) and the test suite must fail for each. A mutant that survives either exposes a missing test, which is then written, or is shown to be equivalent (it changes no observable behaviour). In M6: 16 mutants, 14 caught (2 of them only after adding the tests the first run showed were missing). The other 2 cannot be caught by a test: one handles the last 8 bytes of the CRC one byte at a time, which gives the same result; the other writes only the low 4 bytes of the size field, which makes no difference below 4 GiB.
