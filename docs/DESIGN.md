# Design

This document explains how tcomp is put together and the conventions every module follows. It grows with each milestone.

## Architecture (M1)

```
             src/main.c  (CLI: argument parsing, opening files, exit codes, messages)
                  │
                  ▼
        ┌──────────────────────────────────┐
        │ libtcomp.a                        │
        │                                   │
        │  container.c   header + method    │
        │                dispatch           │
        │  bitio.c       bit writer/reader  │  (used by the codecs from M2)
        │  status.c      error messages     │
        └──────────────────────────────────┘
```

The CLI and the unit tests both link against the same static library, so tests exercise exactly the code the CLI ships.

Planned modules, each added in its milestone: `huffman.c` (M2), `lz77.c` (M3), `crc32.c` (M5), `matcher.c` (M6), `block.c` (M7), `threads.c` (M9).

## Modules

### bitio (`include/tcomp/bitio.h`, M1)

Packs variable-width fields (0–32 bits) into bytes and unpacks them. Bits are MSB-first: the first bit written is bit 7 (0x80) of the first byte. See [decision 0003](decisions/0003-bit-order-and-bitio-design.md).

| Type | Owns memory? | Key operations |
| --- | --- | --- |
| `tcomp_bitwriter` | Yes, a growable buffer; free with `tcomp_bw_free()` | `write_bits`, `write_bit`, `flush` (pad to byte), `data`/`size` |
| `tcomp_bitreader` | No, borrows the caller's buffer | `read_bits`, `read_bit`, `peek_bits`, `skip_bits`, `align` |

How the writer works: pending bits sit right-aligned in a 64-bit accumulator. Each write shifts the accumulator left by the field width and ORs the value in, then moves every complete byte (top 8 pending bits) into the buffer. Fewer than 8 bits remain pending between calls.

How the reader works: to peek N bits at bit position `pos`, it loads the 5 bytes starting at `pos / 8` into a 40-bit number (bytes past the end count as 0), shifts right so the wanted field sits at the bottom, and masks it. Five bytes cover the worst case: a 32-bit field starting 7 bits into a byte.

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
| Negative tests | `tests/roundtrip.sh` | Bad input exits with code 1, leaves no output file, triggers no sanitizer |
| Fuzzing | `fuzz/` (M8) | The decoder survives arbitrary bytes |

Debug builds run all tests under AddressSanitizer and UndefinedBehaviorSanitizer. Sanitizer failures exit with code 86 so they are never confused with a normal error.
