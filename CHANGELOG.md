# Changelog

All notable changes, one section per milestone tag. Newest first.

## v0.3-lz77 (M3): 2026-10-08

### Added
- LZ77 module (`include/tcomp/lz77.h`, `src/lz77.c`): literal/match tokens with DEFLATE parameters (matches 3–258 bytes, 32 KB window), a naive greedy matcher, and a validating expander.
- LZ77 method (id 2): original size, then 9-bit literals and 24-bit matches ([docs/FORMAT.md](docs/FORMAT.md)). `tcomp compress -m lz77`.
- Streaming LZ77 decoder with a 96 KB sliding buffer: constant memory, whatever the size field claims. Rejects matches reaching before the start of the output or past the declared size.
- 10 LZ77 unit tests (exact tokenizations, overlap, window limit, length cap, invalid tokens, seeded random roundtrips over 5 window sizes) and 3 container tests built from hand-made token streams, including 300 KB of matches at the full 32 KB distance.
- `make corpus` now also round-trips every Canterbury file with LZ77 and prints both ratios.
- Decision note 0005; LZ77 results and the naive matcher speed baseline in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

### Changed
- Roundtrip script tests LZ77 on every file except 1 MiB of random data (too slow for the naive matcher under sanitizers) and adds a 16 KiB random file for the worst case (95 checks).
- `-m auto` still chooses between STORE and HUFFMAN only; LZ77 joins once it is fast (M6).
- Version string bumped to 0.3.0.

## v0.2-huffman (M2): 2026-10-06

### Added
- Huffman module (`include/tcomp/huffman.h`, `src/huffman.c`): length-limited code lengths (15 bits), canonical codes per RFC 1951, and a single-table decoder. Works for alphabets up to 1024 symbols.
- HUFFMAN method (id 1) in the `.tcmp` container: original size, 4-bit code lengths, then one code per byte ([docs/FORMAT.md](docs/FORMAT.md)).
- CLI option `-m auto|huffman|store`. `auto` (default) picks HUFFMAN only when it is strictly smaller, so output never exceeds input + 6 bytes.
- Decoder hardening: rejects impossible code lengths, unused codes, oversized size fields (before writing any output), non-zero padding and trailing data. New status `TCOMP_ERR_CORRUPT`.
- `make corpus`: downloads and SHA-1-verifies the Canterbury corpus, round-trips every file, prints ratios. Runs in CI under sanitizers.
- 12 Huffman unit tests (CLRS and RFC 1951 known answers, entropy-bound property test, length limiting, decoder errors) and 11 new container tests including hand-built hostile files.
- Decision note 0004; Canterbury results and analysis in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

### Changed
- `tcomp_compress_stream()` takes a `tcomp_method` argument.
- Roundtrip script tests every file with every method (74 checks).
- Library I/O buffers are allocated per call instead of `static`, so the library is safe to use from multiple threads (needed for M9).
- CI: `actions/checkout@v5` (Node 24) and runners pinned to `ubuntu-24.04`.
- Version string bumped to 0.2.0.

## v0.1-bitio (M1): 2026-10-06

### Added
- Bit-level I/O module (`include/tcomp/bitio.h`, `src/bitio.c`): MSB-first bit writer with a growable buffer, and a bounds-checked bit reader with read, peek, skip and align.
- 16 bit I/O unit tests: exact expected bytes, 32-bit fields at every bit offset, truncation and invalid-argument cases, and 250,000 seeded random-width fields round-tripped.
- Decision note 0003: bit order and bit I/O design.

### Changed
- Unit tests split into one file per module (`tests/unit/test_<module>.c`) with a shared runner in `tests/unit/main.c`.
- Version string bumped to 0.1.0.

## v0.0-setup (M0): 2026-10-05

### Added
- Makefile with debug (ASan + UBSan) and release builds, `test`, `format` and `format-check` targets.
- `.tcmp` container version 0 with the STORE method ([docs/FORMAT.md](docs/FORMAT.md)).
- CLI: `compress`, `decompress`, `--version`, `--help`, `-` for stdin/stdout.
- Unit test framework and 13 unit tests.
- Roundtrip script: 11 edge-case files through files and pipes, plus negative tests for invalid input.
- GitHub Actions CI: gcc and clang × debug and release, plus a formatting check.
- Docs: README, ROADMAP, FORMAT, DESIGN, BENCHMARKS, decision notes 0001 and 0002.
