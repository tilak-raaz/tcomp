# Roadmap

tcomp is built in milestones. Each one ends with working code, passing tests, updated docs and a git tag, so the repository is always in a presentable state.

## Milestones

| # | Milestone | Done when | Tag |
| --- | --- | --- | --- |
| M0 | Setup: build, tests, CI, docs skeleton, STORE container | `make test` passes in CI with gcc and clang | `v0.0-setup` |
| M1 | Bit I/O: `bitwriter` / `bitreader` | Random bit sequences of random widths round-trip | `v0.1-bitio` |
| M2 | Huffman: static, then canonical codes | Edge cases + Canterbury corpus round-trip | `v0.2-huffman` |
| M3 | LZ77: naive matcher, literal/match tokens | Round-trips; ratio measured | `v0.3-lz77` |
| M4 | LZ77 + Huffman, DEFLATE-style two alphabets | Beats plain Huffman on text | `v0.4-deflate` |
| M5 | `.tcmp` v1: full format, CRC32, `info` command | Corrupted files are rejected | `v1.0` |
| M6 | Hash-chain matcher + `tcomp bench` | Speedup over naive measured | `v1.1-hashchain` |
| M7 | Blocks + streaming, bounded memory | Large pipes compress with flat RAM | `v1.2-streaming` |
| M8 | Fuzzing (libFuzzer) and robustness | Hours of fuzzing, no crash | `v1.3-fuzz` |
| M9 | Multithreading over blocks | Speedup and ratio loss at 1/2/4/8 threads | `v1.4-threads` |
| M10+ | Optional: BWT mode, lazy matching or ANS | Benchmarked against LZ77 mode | — |

## Progress

- [x] M0 Setup
- [ ] M1 Bit I/O
- [ ] M2 Huffman
- [ ] M3 LZ77
- [ ] M4 LZ77 + Huffman
- [ ] M5 `.tcmp` v1 (resume-ready)
- [ ] M6 Hash chains + benchmarks
- [ ] M7 Blocks + streaming
- [ ] M8 Fuzzing
- [ ] M9 Multithreading
- [ ] M10+ Optional advanced mode

## Definition of done (every milestone)

1. `make test` passes in debug (sanitizers) and release, with gcc and clang.
2. Docs touched by the milestone are updated (FORMAT, DESIGN, BENCHMARKS, README).
3. A decision note exists in `docs/decisions/` for each non-obvious choice.
4. `CHANGELOG.md` has an entry and the milestone tag is pushed.
