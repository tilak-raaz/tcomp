# Changelog

All notable changes, one section per milestone tag. Newest first.

## v0.4-hashchain (M4): 2026-10-08

### Added
- Hash-chain LZ77 matcher, `tcomp_lz77_parse(..., max_chain, ...)`: 3-byte multiplicative hash, `head` table and a 32 KB `prev` ring. With no chain limit its tokens are identical to the naive matcher's; the default limit is 128 candidates per position.
- Benchmark harness (`bench/bench.c`, `make bench`, `make bench-quick`): in-process, best-of-5 timings of the naive matcher against 8 chain limits, and of every method end to end with each result verified.
- Canterbury large set (E.coli, bible.txt, world192.txt; 11 MB) in `bench/fetch-corpus.sh`, SHA-1-verified.
- Differential tests: hash chains vs naive on 100 random input/window combinations and on 80 KB data after the chain ring wraps. A test pinning what a chain limit counts.
- CI job running `make bench-quick` as a smoke test.
- Decision note 0006; matcher sweep and per-file chain analysis in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

### Changed
- Compression uses hash chains: **205× faster than naive** at the default limit (13.7 MB/s on 14 MB), **57× faster with byte-identical output** at no limit.
- `-m auto` now picks the smallest of STORE, HUFFMAN and LZ77 (2.93× on the benchmark set, against 2.44× for LZ77 and 2.24× for Huffman alone).
- The naive matcher is kept as `tcomp_lz77_parse_naive()`, the reference for tests and benchmarks.
- Roadmap reordered: hash chains moved from M6 to M4; LZ77 + Huffman is now M5 and the v1 format M6. Later tags renumbered.
- Roundtrip script no longer skips LZ77 on 1 MiB of random data (96 checks, 3 s instead of 12 s under sanitizers); CI corpus job 3 s instead of 78 s.
- Version string bumped to 0.4.0.

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
