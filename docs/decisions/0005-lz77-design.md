# 0005: DEFLATE-style LZ77 tokens, naive greedy matcher, fixed-width coding

- **Date:** 2026-10-08
- **Milestone:** M3
- **Status:** Accepted

## Context

Huffman (M2) only exploits how often each byte occurs. Real data repeats whole strings: words, markup, code, runs of zeros. LZ77 replaces a repeat with a reference to its earlier copy. M3 adds LZ77 on its own, so its effect can be measured before M4 combines it with Huffman.

## Options considered

**Token shape**

1. **`(distance, length, next byte)` triples** (LZ77 as published in 1977). Every token carries a literal even when it is not needed.
2. **Literal *or* match** (LZSS, used by DEFLATE). A one-bit flag says which. Matches shorter than 3 bytes are not worth their cost, so they are sent as literals.

**Parameters:** DEFLATE's: matches of 3 to 258 bytes reaching back up to 32 KB. Proven, small enough that a 15-bit distance and 8-bit length field cover them exactly, and directly reusable by M4.

**Match finding**

1. **Naive search:** try every position in the window. O(n × window). Easy to verify.
2. **Hash chains:** index positions by their next 3 bytes and visit only candidates that share them. Much faster, more code.

**Parsing:** greedy (take the longest match now) or lazy (check whether the next position has a longer one). Lazy gains a few percent and belongs with performance work.

**Bit encoding for M3:** fixed fields (9-bit literal, 24-bit match) or entropy-coded. Fixed fields isolate what LZ77 alone achieves; entropy coding is M4.

## Decision

- Literal-or-match tokens, DEFLATE parameters (3–258 bytes, 32 KB window).
- **Naive greedy matcher**, nearest match on ties (smaller distances will cost fewer bits once M4 Huffman-codes them). One standard shortcut: a candidate is skipped at once unless it also matches the byte at the current best length, since otherwise it cannot win.
- **Fixed-width coding:** literal = `0` + 8 bits; match = `1` + 8-bit (length − 3) + 15-bit (distance − 1).
- The decoder streams: it keeps only the last 32 KB of output in a 96 KB buffer and writes the rest out, so its memory use is constant whatever the file claims.
- **AUTO does not use LZ77 yet.** At 0.2 MB/s the naive matcher is too slow to be a default.

## Consequences

- On the Canterbury corpus LZ77 alone reaches 2.67×, against 2.14× for Huffman, and wins on every file (`ptt5`: 7.04× vs 4.81×).
- Compression runs at about 0.2 MB/s on text and 0.04 MB/s on random data, where no candidate ever matches and all 32,768 are tried at every position. Decompression runs at about 76 MB/s: decoding is copying, all the work is in finding matches. This is the baseline the hash-chain matcher (M6) is measured against.
- Tests skip LZ77 on the 1 MiB random file (minutes under sanitizers) and use a 16 KiB random file for the worst case instead.
- Fixed-width fields waste bits: every literal costs 9 bits even when it is common, and every distance costs 15 bits even when it is small. M4 replaces them with Huffman codes over DEFLATE's literal/length and distance alphabets.
