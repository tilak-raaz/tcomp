# 0006: Hash-chain matcher with a 128-candidate limit

- **Date:** 2026-10-08
- **Milestone:** M4
- **Status:** Accepted

## Context

The naive matcher from M3 tries every position in the 32 KB window at every byte. On the full benchmark set (Canterbury + large, 14 MB) it parses at 0.07 MB/s and needs 3.5 minutes; on random data it manages 0.04 MB/s. That made LZ77 unusable as a default and made the sanitizer test runs slow.

## Options considered

1. **Hash chains** (zlib, gzip). Index each position by a hash of its next 3 bytes. Keep `head[hash]` = latest position with that hash and `prev[pos]` = the previous one. A match of 3+ bytes must start with the same 3 bytes, so walking the chain visits every useful candidate and skips the rest.
2. **Binary trees** (LZMA's `bt4`). Positions sorted in a tree per hash: finds the longest match in fewer steps on highly repetitive data, but more complex and slower to update.
3. **Suffix arrays.** Optimal match finding, but built over the whole input up front: memory-hungry and a poor fit for streaming (M7).

## Decision

- **Hash chains**, 15-bit multiplicative hash of 3 bytes, `head` of 32,768 entries, `prev` as a 32,768-entry ring indexed by `pos % 32768`. A ring slot is reused only after its position has left the window, and the walk stops at the window edge, so stale links are never followed.
- **Every position is indexed**, including those inside a match, so later matches can start anywhere.
- **Candidates are visited nearest first** and only a strictly longer match replaces the best one. With no chain limit the result is therefore byte-for-byte the naive matcher's output; a unit test checks exactly that, including after the ring wraps.
- **A chain limit of 128 candidates per position** (zlib's level-6 value). The sweep in BENCHMARKS.md shows text files within 0.5% of the unlimited ratio at 128, while two degenerate files (`E.coli`, a 4-letter genome, and `ptt5`, long zero runs) keep gaining from deeper searches. Callers can pass any limit, or unlimited.
- The naive matcher stays as `tcomp_lz77_parse_naive()`: the reference for differential tests and the benchmark baseline.
- **AUTO now considers LZ77**: it plans Huffman, parses LZ77, and writes whichever of STORE, HUFFMAN and LZ77 is smallest.

## Consequences

- Unlimited hash chains produce identical output 57× faster than naive. The default (128) is 205× faster at 13.7 MB/s, for 6% worse compression overall on the 14 MB set (1% on the 2.8 MB Canterbury set; the rest is `E.coli`).
- Sanitizer test runs drop from 12 s to 3 s, and the CI corpus job from 78 s to 3 s.
- Memory: 512 KB of chain tables per parse (two arrays of 32,768 `size_t`), independent of input size.
- Not yet done, left for later performance work: lazy matching (check the next position before committing to a match, a few percent better), a "nice length" early exit, and halving the chain once a good match is found (zlib's `good_length`).
- A weak hash would only make chains longer, never output wrong; unit tests cannot see that, `make bench` can.
