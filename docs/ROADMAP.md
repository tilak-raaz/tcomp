# Roadmap

tcomp is built in milestones. Each one ends with working code, passing tests, updated docs and a git tag, so the repository is always in a presentable state.

## Milestones

| # | Milestone | Done when | Tag |
| --- | --- | --- | --- |
| M0 | Setup: build, tests, CI, docs skeleton, STORE container | `make test` passes in CI with gcc and clang | `v0.0-setup` |
| M1 | Bit I/O: `bitwriter` / `bitreader` | Random bit sequences of random widths round-trip | `v0.1-bitio` |
| M2 | Huffman: static, then canonical codes | Edge cases + Canterbury corpus round-trip | `v0.2-huffman` |
| M3 | LZ77: naive matcher, literal/match tokens | Round-trips; ratio measured | `v0.3-lz77` |
| M4 | Hash-chain matcher + benchmark harness (`make bench`) | Speedup over naive measured; AUTO uses LZ77 | `v0.4-hashchain` |
| M5 | LZ77 + Huffman, DEFLATE-style two alphabets | Beats both LZ77 and Huffman alone | `v0.5-deflate` |
| M6 | `.tcmp` v1: full format, CRC32, `info` command | Corrupted files are rejected | `v1.0` |
| M7 | Blocks + streaming, bounded memory | Large pipes compress with flat RAM | `v1.1-streaming` |
| M8 | Fuzzing (libFuzzer) and robustness | Hours of fuzzing, no crash | `v1.2-fuzz` |
| M9 | Multithreading over blocks | Speedup and ratio loss at 1/2/4/8 threads | `v1.3-threads` |
| M10+ | Optional: BWT mode, lazy matching or ANS | Benchmarked against LZ77 mode | — |

**Order change (M4):** the hash-chain matcher was planned for M6. It moved to M4, straight after the naive matcher, so that the slow matcher (0.07–0.2 MB/s) would not be carried through LZ77 + Huffman and the v1.0 release, and so the speedup was measured while the naive baseline was fresh. LZ77 + Huffman and the v1 format each moved one milestone later.

## Progress

- [x] M0 Setup
- [x] M1 Bit I/O
- [x] M2 Huffman
- [x] M3 LZ77
- [x] M4 Hash chains + benchmarks
- [ ] M5 LZ77 + Huffman
- [ ] M6 `.tcmp` v1 (resume-ready)
- [ ] M7 Blocks + streaming
- [ ] M8 Fuzzing
- [ ] M9 Multithreading
- [ ] M10+ Optional advanced mode

## Definition of done (every milestone)

1. `make test` passes in debug (sanitizers) and release, with gcc and clang.
2. Docs touched by the milestone are updated (FORMAT, DESIGN, BENCHMARKS, README).
3. A decision note exists in `docs/decisions/` for each non-obvious choice.
4. `CHANGELOG.md` has an entry and the milestone tag is pushed.
