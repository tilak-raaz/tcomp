# 0007: LZH, DEFLATE's dynamic-Huffman layout over LZ77 tokens

- **Date:** 2026-10-12
- **Milestone:** M5
- **Status:** Accepted

## Context

After M4, Huffman and LZ77 each capture one kind of redundancy: Huffman the skew in byte frequencies, LZ77 repeated strings. The LZ77 method still spends a fixed 9 bits on every literal and 24 on every match, however common or close. Combining the two is what gzip does; M5 does the same.

## Options considered

**How to Huffman-code tokens**

1. **One alphabet for everything** (literals, lengths and distances as separate symbols in a single code). Simple, but lengths and distances have very different statistics and would share one table.
2. **DEFLATE's two alphabets.** Literals and lengths share one code (both answer "what comes next?"); distances get their own. Lengths and distances are bucketed into 29 and 30 symbols with extra bits for the exact value, so the alphabets stay small and Huffman tables cheap, while common short lengths and near distances get short codes.
3. **Separate codes for literals, lengths and distances, plus a flag.** The flag bit is waste: in option 2 the symbol itself says whether a literal or a length follows.

**How to send the code tables**

1. **4-bit length per symbol** (as the HUFFMAN method does): 316 symbols × 4 bits = 158 bytes per file.
2. **DEFLATE's scheme:** concatenate both tables' lengths, run-length code them (16 = repeat previous 3–6 times, 17 = 3–10 zeros, 18 = 11–138 zeros), Huffman-code the result with a third, 19-symbol code, and send that code's lengths in 3-bit fields, ordered so the rarely used ones come last and can be dropped.

## Decision

- **DEFLATE's two alphabets** with RFC 1951's length and distance tables exactly (286 literal/length symbols, 256 reserved; 30 distance symbols).
- **DEFLATE's table encoding**: HLIT/HDIST/HCLEN counts, the permuted code-length order, run-length symbols 16/17/18. Literal/length and distance codes limited to 15 bits, the code-length code to 7.
- **One set of tables per file** (one "block"). Per-block tables come with blocks in M7.
- Same greedy hash-chain parse as LZ77 (default chain 128); LZ77 and LZH share the token array, so AUTO parses once and plans both.
- **Exact planning:** `tcomp_lzh_plan` computes the bitstream size to the bit before writing; a unit test checks it against the bits actually written.
- The decoder reuses the LZ77 method's streaming expansion: only the token reader differs.
- Not bit-compatible with gzip: tcomp's bitstream is MSB-first (decision 0003) and wraps the data in its own container.

## Consequences

- Canterbury corpus: **3.58×**, against 2.64× for LZ77 alone, 2.14× for Huffman, and gzip -9's 3.85× (gzip -6: 3.83×, gzip -1: 3.25×). Code tables cost 62–84 bytes on Canterbury text and source files and 126–136 bytes on its binaries, instead of 158.
- Search depth is not the remaining gap: an unlimited chain only reaches 3.60×. What gzip adds is lazy matching and new tables every few thousand symbols; the second arrives with blocks (M7), the first is an M10 option.
- A greedy parser takes any 3-byte match, even where literals would be cheaper. On `E.coli` (4-letter DNA) LZH reaches 3.46× while plain Huffman reaches 4.00×, ahead of gzip -9's 3.57×. AUTO picks Huffman there, which is why AUTO (3.57× on the 14 MB set) beats LZH alone (3.41×). Cost-aware parsing would fix this inside LZH.
- Compression speed is set by the matcher (about 11 MB/s); decoding a Huffman code per token costs about 20% of fixed-width LZ77's decompression speed (108 vs 134 MB/s).
