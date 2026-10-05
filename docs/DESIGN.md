# Design

This document explains how tcomp is put together and the conventions every module follows. It grows with each milestone.

## Architecture (M0)

```
             src/main.c  (CLI: argument parsing, opening files, exit codes, messages)
                  │
                  ▼
        ┌──────────────────────────────────┐
        │ libtcomp.a                        │
        │                                   │
        │  container.c   header + method    │
        │                dispatch           │
        │  status.c      error messages     │
        └──────────────────────────────────┘
```

The CLI and the unit tests both link against the same static library, so tests exercise exactly the code the CLI ships.

Planned modules, each added in its milestone: `bitio.c` (M1), `huffman.c` (M2), `lz77.c` (M3), `crc32.c` (M5), `matcher.c` (M6), `block.c` (M7), `threads.c` (M9).

## Conventions

**Errors.** Every function that can fail returns `tcomp_status` (`include/tcomp/status.h`). `TCOMP_OK` is zero. Library code never prints, never calls `exit()`, and never aborts on bad input. Only `src/main.c` turns a status into a message and an exit code. See [decision 0002](decisions/0002-error-handling.md).

**Streams, not whole files.** The library API takes `FILE *` streams and reads until EOF. This keeps stdin/stdout support free and prepares for streaming in M7.

**Memory ownership.** Each header comment says who owns any pointer passed in or returned. Callers free what they allocate; the library frees what it allocates.

**Untrusted input.** The decompressor treats its input as hostile: every length, index and table read from a file is validated before use. This is checked by negative tests now and fuzzing from M8.

**Headers.** Public headers live in `include/tcomp/` and are included as `"tcomp/name.h"`. Internal helpers are `static`; `-Wmissing-prototypes` enforces this.

## Testing strategy

| Layer | Where | What it proves |
| --- | --- | --- |
| Unit tests | `tests/unit/` | Each module's functions in isolation, including error paths |
| Roundtrip tests | `tests/roundtrip.sh` | `decompress(compress(x)) == x` through the real CLI, via files and pipes, on edge-case data |
| Negative tests | `tests/roundtrip.sh` | Bad input exits with code 1, leaves no output file, triggers no sanitizer |
| Fuzzing | `fuzz/` (M8) | The decoder survives arbitrary bytes |

Debug builds run all tests under AddressSanitizer and UndefinedBehaviorSanitizer. Sanitizer failures exit with code 86 so they are never confused with a normal error.
