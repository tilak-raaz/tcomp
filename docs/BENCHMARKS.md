# Benchmarks

Ratios are tracked from M2, speed from M3. Since M4, `make bench` reproduces the speed tables (release build, in-process, best of 5; about 4.5 minutes because of the naive matcher, or `make bench-quick` without it).

## M6: format v1 and CRC-32

Measured 2026-10-13. Release build, gcc 13.3 `-O2`, Intel Xeon @ 2.80 GHz (a shared 2-vCPU virtual machine), one thread.

**Corruption detection.** A 3,601-byte text file (`tests/fixtures/sample.txt`) compressed with each method; then every bit of the compressed file flipped in turn, and the file cut at every length, each result decoded. "Same" means the flip still decoded to the exact original (for example a match distance changed to point at identical text), which is harmless. "Wrong" means the decoder reported success with different data.

| Method | Bit flips | tcomp 0.5: rejected | same | **wrong** | tcomp 1.0: rejected | same | **wrong** |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| store | 28,856 / 28,960 | 48 | 0 | **28,808 (99.8%)** | 28,960 | 0 | **0** |
| huffman | 19,728 / 19,768 | 3,959 | 0 | **15,769 (79.9%)** | 19,768 | 0 | **0** |
| lz77 | 20,800 / 20,840 | 7,381 | 10 | **13,409 (64.5%)** | 20,830 | 10 | **0** |
| lzh | 14,416 / 14,456 | 7,476 | 7 | **6,933 (48.1%)** | 14,449 | 7 | **0** |

(Flip counts differ by 40 because each v1 file is 5 bytes longer.) Truncations: tcomp 0.5 accepted 3,601 of 3,607 cut STORE files, writing a shorter file with exit code 0. tcomp 1.0 rejects every truncation of every method. Even the compressed methods, which do have structure, decoded half or more of all flips to wrong output without a checksum: a Huffman bitstream is dense, so most flips just turn one valid code into another. `make test` repeats this check (on a smaller file) for every method on every run.

**CRC-32 speed** (64 MB buffer):

| Implementation | MB/s |
| --- | ---: |
| bit at a time (the definition) | 97 |
| one 256-entry table, a byte at a time | 387 |
| **slicing-by-8** (tcomp) | **2,105** |

`make bench` reports the slicing-by-8 figure on the corpus as well (about 2,000 MB/s).

**What the checksum costs.** Instruction counts (callgrind, decompressing `bible.txt`): the CRC is **4.0%** of all instructions for LZH and **2.1%** for HUFFMAN. A cache simulation (cachegrind, 32 KB L1) shows it missing L1 once per 64-byte line of data, which is the minimum for reading the data once.

Wall-clock, tcomp 0.5 against 1.0 on the 14 files (13,970,266 bytes), best of 4 interleaved `make bench-quick` runs:

| Method | Compressed (1.0) | Compress MB/s, 0.5 → 1.0 | Decompress MB/s, 0.5 → 1.0 |
| --- | ---: | ---: | ---: |
| store | 13,970,532 | 1,214 → 395 | 1,307 → 452 |
| huffman | 6,251,322 | 111 → 92 | 82 → 62 |
| lz77 | 5,716,187 | 12.7 → 11.5 | 140 → 121 |
| lzh | 4,093,742 | 11.0 → 10.3 | 114 → 102 |
| **auto** | **3,911,731** | 10.4 → 10.0 | 99 → 85 |

Ratios are unchanged (each file is 5 bytes larger: 3.58× on Canterbury, 785,314 bytes, and 3.571× on the 14 files).

**Analysis**

- **The integrity check costs about 4–5% of decompression work**, which matches its standalone speed: about 0.5 ns per byte, against 9–16 ns per byte to decode.
- **The wall-clock slowdown is larger than that and is not fully explained.** A build of 1.0 with the CRC removed decompresses as fast as 0.5, so the slowdown does come with the CRC. But it appears only when output is written: decompressing to a discarding sink (`tcomp verify`), the CRC made no measurable difference. Checksumming in 16 KB slices, so each slice is still in L1 when it is written, did not help either. The test machine is a shared virtual machine whose timings vary 10% between runs; this should be re-measured on dedicated hardware with hardware performance counters before optimising.
- **STORE is the one real regression**: compression now reads the whole input before writing (the header needs its size), and at ~1 GB/s a 2 GB/s CRC is no longer negligible. It is still faster than any other method by 4×. M7's blocks restore streaming.
- **A faster CRC exists** (carry-less multiplication, PCLMULQDQ on x86 or PMULL on ARM, at 10+ GB/s) but needs CPU-specific code. At 4% of decompression it is not worth it yet.

## M5: LZH (LZ77 + Huffman, DEFLATE-style)

Measured 2026-10-12. Release build, gcc 13.3 `-O2`, Intel Xeon @ 2.80 GHz, one thread. Sizes include tcomp's 14-byte header; gzip's include its own (about 20 bytes plus the file name).

**Canterbury corpus** (`make corpus` reproduces the ratios):

| File | Size | Huffman | LZ77 | **LZH** | gzip -6 | gzip -9 | LZH vs gzip -9 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| alice29.txt | 152,089 | 87,836 | 73,169 | **55,611** | 54,435 | 54,191 | +2.6% |
| asyoulik.txt | 125,179 | 75,948 | 67,498 | **50,323** | 48,951 | 48,829 | +3.1% |
| cp.html | 24,603 | 16,341 | 10,894 | **8,186** | 7,999 | 7,981 | +2.6% |
| fields.c | 11,150 | 7,168 | 4,457 | **3,182** | 3,143 | 3,136 | +1.5% |
| grammar.lsp | 3,721 | 2,312 | 1,852 | **1,250** | 1,246 | 1,246 | +0.3% |
| kennedy.xls | 1,029,744 | 462,674 | 345,452 | **245,229** | 206,779 | 209,733 | +16.9% |
| lcet10.txt | 426,754 | 250,715 | 193,283 | **148,135** | 144,885 | 144,429 | +2.6% |
| plrabn12.txt | 481,861 | 275,744 | 269,100 | **200,198** | 195,208 | 194,277 | +3.0% |
| ptt5 | 513,216 | 106,701 | 76,464 | **57,917** | 56,443 | 52,382 | +10.6% |
| sum | 38,240 | 25,787 | 18,388 | **13,456** | 12,924 | 12,772 | +5.4% |
| xargs.1 | 4,227 | 2,744 | 2,600 | **1,772** | 1,756 | 1,756 | +0.9% |
| **Total** | **2,810,784** | **1,313,970** | **1,063,157** | **785,259** | **733,769** | **730,732** | **+7.5%** |
| **Ratio** | | 2.14× | 2.64× | **3.58×** | 3.83× | 3.85× | |

gzip -1 totals 865,535 bytes (3.25×): LZH beats it by 9%.

**Where the gap to gzip comes from.** LZH with a deeper search, same greedy parse:

| LZH matcher | Total | Ratio |
| --- | ---: | ---: |
| chain 128 (default) | 785,259 | 3.579 |
| chain 1024 | 781,217 | 3.598 |
| unlimited | 780,751 | 3.600 |

**Large files** (ratio by method):

| File | Size | Huffman | LZ77 | LZH | gzip -9 |
| --- | ---: | ---: | ---: | ---: | ---: |
| E.coli | 4,638,690 | **4.00** | 2.20 | 3.46 | 3.57 |
| bible.txt | 4,047,392 | 1.82 | 2.54 | **3.32** | 3.44 |
| world192.txt | 2,473,400 | 1.59 | 2.60 | **3.31** | 3.43 |

**Methods end to end** (`make bench`, all 14 files, 13,970,266 bytes, best of 5, every result verified):

| Method | Compressed | Ratio | Compress MB/s | Decompress MB/s |
| --- | ---: | ---: | ---: | ---: |
| store | 13,970,350 | 1.000 | 1,022 | 1,046 |
| huffman | 6,251,252 | 2.235 | 106 | 77 |
| lz77 | 5,716,117 | 2.444 | 12.0 | 134 |
| lzh | 4,093,672 | 3.413 | 10.7 | 108 |
| **auto** | **3,911,661** | **3.571** | 10.4 | 99 |

**Analysis**

- **Combining the two stages pays off multiplicatively.** LZH is 26% smaller than LZ77 and 40% smaller than Huffman on the Canterbury corpus: LZ77 removes repeats, then Huffman shortens what is left, including the length and distance fields LZ77 spent 24 bits on.
- **Text and source code are within 0.3–3.1% of gzip -9.** The remaining gap is not search depth: an unlimited chain gains only 0.6%. gzip also uses *lazy matching* (before taking a match, check whether the next position starts a longer one) and starts *new Huffman tables every few thousand symbols*. The second arrives with blocks in M7; the first is an M10 option.
- **The two big gaps are files whose statistics change partway**: `kennedy.xls` (+17%, a spreadsheet mixing headers, numbers and padding) and `ptt5` (+11%, a fax image with blank and dense regions). One set of tables per file fits neither part well. This is exactly what per-block tables fix.
- **A greedy parser can lose to plain Huffman.** On `E.coli` (DNA: 4 letters, 2 bits each with Huffman) every 3-base repeat becomes a match costing more than the 6 bits of literals it replaces. AUTO picks Huffman there, reaching 4.00× against gzip -9's 3.57×, which is why AUTO (3.57×) beats LZH alone (3.41×) on the 14 MB set.
- **Code tables are small.** They take 62–84 bytes on text and source files and 126–136 bytes on the two binaries, where nearly every byte value occurs, against 158 bytes for plain 4-bit lengths.
- **Speed is the matcher's.** LZH compresses at 10.7 MB/s against LZ77's 12.0; decoding a Huffman code per token costs about 20% of LZ77's decompression speed (108 vs 134 MB/s).

## M4: hash-chain matcher

Measured 2026-10-08 with `make bench` on the Canterbury corpus plus the Canterbury large set (E.coli, bible.txt, world192.txt): 14 files, 13,970,266 bytes. Release build, gcc 13.3 `-O2`, Intel Xeon @ 2.80 GHz, one thread.

**Matchers.** Greedy LZ77 parse of all 14 files; "Tokens" is the size in the LZ77 method's fixed-width format.

| Matcher | Parse time | MB/s | Speedup vs naive | Tokens (bytes) | Ratio |
| --- | ---: | ---: | ---: | ---: | ---: |
| naive (M3) | 209.8 s | 0.07 | 1× | 5,382,077 | 2.596 |
| hash chain, unlimited | 3.71 s | 3.77 | **57×** | 5,382,077 | 2.596 |
| hash chain, 1024 | 3.47 s | 4.02 | 60× | 5,383,622 | 2.595 |
| hash chain, 256 | 1.83 s | 7.63 | 115× | 5,539,517 | 2.522 |
| **hash chain, 128 (default)** | **1.02 s** | **13.68** | **205×** | **5,715,921** | **2.444** |
| hash chain, 64 | 0.60 s | 23.24 | 349× | 5,938,454 | 2.353 |
| hash chain, 16 | 0.26 s | 54.40 | 817× | 6,613,058 | 2.113 |
| hash chain, 4 | 0.13 s | 111.18 | 1,669× | 7,790,424 | 1.793 |
| hash chain, 1 | 0.09 s | 153.17 | 2,300× | 9,419,772 | 1.483 |

**Where the chain limit costs ratio** (fixed-width LZ77 ratio by chain limit):

| File | 16 | 64 | 128 | 256 | unlimited |
| --- | ---: | ---: | ---: | ---: | ---: |
| alice29.txt | 1.92 | 2.05 | 2.08 | 2.09 | 2.09 |
| lcet10.txt | 2.05 | 2.18 | 2.21 | 2.22 | 2.22 |
| plrabn12.txt | 1.66 | 1.77 | 1.79 | 1.80 | 1.80 |
| bible.txt | 2.29 | 2.49 | 2.54 | 2.57 | 2.59 |
| world192.txt | 2.49 | 2.58 | 2.60 | 2.61 | 2.61 |
| kennedy.xls | 2.96 | 2.98 | 2.98 | 2.99 | 3.00 |
| ptt5 | 6.55 | 6.67 | 6.71 | 6.83 | 7.04 |
| E.coli | 1.70 | 2.03 | 2.20 | 2.37 | 2.55 |

**Methods end to end** (all 14 files, `tcomp_compress_stream` / `tcomp_decompress_stream` through in-memory streams, every result decompressed and compared):

| Method | Compressed | Ratio | Compress MB/s | Decompress MB/s |
| --- | ---: | ---: | ---: | ---: |
| store | 13,970,350 | 1.000 | 1,115 | 1,143 |
| huffman | 6,251,252 | 2.235 | 124 | 88 |
| lz77 | 5,716,117 | 2.444 | 13.2 | 167 |
| auto | 4,767,141 | 2.931 | 13.5 | 136 |

**Analysis**

- **Same output, 57× faster.** With no chain limit, hash chains produce exactly the naive matcher's tokens: every match of 3+ bytes shares its first 3 bytes, hence its hash, so skipping other positions loses nothing. The speedup is pure algorithm.
- **The limit trades ratio for speed, unevenly.** On ordinary text, 128 candidates come within 0.5% of unlimited. Two files keep gaining: `E.coli` (4-letter alphabet, so only 64 distinct 3-byte strings and enormous chains of short matches) and `ptt5` (long zero runs, where every position hashes alike). The default, 128, is zlib's level-6 value: 205× faster than naive, 6% behind unlimited overall and 1% on the 2.8 MB Canterbury set.
- **AUTO is now better than any single method** (2.93×): it picks Huffman for `E.coli` (2 bits per base beat LZ77's 9-bit literals) and LZ77 for text. Combining both in one stream (M5) should beat it again.
- **Decompression is 12× faster than compression** for LZ77: the decoder only copies bytes, all the searching happens once, at compression time.

## M3: LZ77 with fixed-width tokens

Measured 2026-10-08 on the Canterbury corpus. Reproduce the size columns with `make corpus`.

| File | Size | Huffman (M2) | LZ77 (M3) | LZ77 ratio | gzip -9 |
| --- | ---: | ---: | ---: | ---: | ---: |
| alice29.txt | 152,089 | 87,836 | 72,716 | 2.09 | 54,191 |
| asyoulik.txt | 125,179 | 75,948 | 67,226 | 1.86 | 48,829 |
| cp.html | 24,603 | 16,341 | 10,887 | 2.26 | 7,981 |
| fields.c | 11,150 | 7,168 | 4,454 | 2.50 | 3,136 |
| grammar.lsp | 3,721 | 2,312 | 1,852 | 2.01 | 1,246 |
| kennedy.xls | 1,029,744 | 462,674 | 343,535 | 3.00 | 209,733 |
| lcet10.txt | 426,754 | 250,715 | 192,242 | 2.22 | 144,429 |
| plrabn12.txt | 481,861 | 275,744 | 267,223 | 1.80 | 194,277 |
| ptt5 | 513,216 | 106,701 | 72,935 | 7.04 | 52,382 |
| sum | 38,240 | 25,787 | 18,197 | 2.10 | 12,772 |
| xargs.1 | 4,227 | 2,744 | 2,600 | 1.63 | 1,756 |
| **Total** | **2,810,784** | **1,313,970** | **1,053,867** | **2.67** | **730,732** |

**Speed baseline** (release build, gcc 13.3 `-O2`, Intel Xeon @ 2.80 GHz, whole corpus, includes process start-up per file):

| Method | Compress | Decompress |
| --- | ---: | ---: |
| Huffman | 56 MB/s | 49 MB/s |
| LZ77, naive matcher | **0.20 MB/s** | 76 MB/s |
| LZ77 on 1 MiB of random data | 0.04 MB/s | — |

**Analysis**

- **LZ77 beats Huffman on every file**, 2.67× against 2.14× overall, without any entropy coding. Repeated strings carry more redundancy than skewed byte frequencies.
- **The biggest gains are on structured data:** `fields.c` (2.50×, source code), `cp.html` (markup), `kennedy.xls` (3.00×, spreadsheet records) and `ptt5` (7.04×, long runs in a fax image, where Huffman could not go below 1 bit per byte).
- **The smallest gain is `plrabn12.txt`** (1.80× vs 1.75×), English verse with few long repeats: there, byte frequencies are most of the redundancy, which is what Huffman captures. That is why combining both (M5) matters.
- **Fixed-width fields leave bits on the table.** Every literal costs 9 bits and every distance 15, however common or small. gzip, which Huffman-codes both, is still 1.44× smaller overall.
- **The naive matcher is 280× slower than Huffman.** Compression is O(n × window): on random data every one of the 32,768 candidates is tried at every position, hence 0.04 MB/s. Decompression is fast because it only copies bytes. The hash-chain matcher (M4, above) targets this number.

## M2: order-0 Huffman on the Canterbury corpus

Measured 2026-10-06 with `tcomp compress -m huffman`, gcc 13.3, `-O2`. Reproduce the tcomp column with `make corpus`.

- **H0 bound:** the order-0 entropy of the file, ⌈n·H/8⌉ bytes. No coder that treats bytes independently can beat it.
- **over:** how far tcomp's output, including its 142-byte header, is above that bound.
- **gzip -9:** for context only. gzip also uses LZ77, which tcomp gets in M3–M5.

| File | Size | tcomp | Ratio | H0 bound | over | gzip -9 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| alice29.txt | 152,089 | 87,836 | 1.73 | 86,837 | 1.2% | 54,191 |
| asyoulik.txt | 125,179 | 75,948 | 1.65 | 75,235 | 0.9% | 48,829 |
| cp.html | 24,603 | 16,341 | 1.51 | 16,082 | 1.6% | 7,981 |
| fields.c | 11,150 | 7,168 | 1.56 | 6,980 | 2.7% | 3,136 |
| grammar.lsp | 3,721 | 2,312 | 1.61 | 2,155 | 7.3% | 1,246 |
| kennedy.xls | 1,029,744 | 462,674 | 2.23 | 459,971 | 0.6% | 209,733 |
| lcet10.txt | 426,754 | 250,715 | 1.70 | 249,071 | 0.7% | 144,429 |
| plrabn12.txt | 481,861 | 275,744 | 1.75 | 272,936 | 1.0% | 194,277 |
| ptt5 | 513,216 | 106,701 | 4.81 | 77,636 | 37.4% | 52,382 |
| sum | 38,240 | 25,787 | 1.48 | 25,473 | 1.2% | 12,772 |
| xargs.1 | 4,227 | 2,744 | 1.54 | 2,589 | 6.0% | 1,756 |
| **Total** | **2,810,784** | **1,313,970** | **2.14** | **1,274,965** | **3.1%** | **730,732** |

**Analysis**

- **Large files are within about 1% of the entropy bound.** Huffman is optimal among codes that spend a whole number of bits per symbol; the remaining gap is that rounding.
- **Small files pay for the header.** The 128-byte length table is 3–6% of `grammar.lsp` and `xargs.1`. A compressed length table (as DEFLATE uses) would recover most of it.
- **`ptt5` is the outlier.** It is a fax image that is mostly zero bytes. When one symbol has probability above 1/2, its ideal code is under 1 bit, but Huffman's shortest code is 1 bit. LZ77 (M3) turns long runs into single matches; arithmetic coding or ANS (M10+) can spend fractions of a bit.
- **gzip is 1.8× smaller overall** because LZ77 removes repeated strings, which an order-0 model cannot see. Closing that gap is the point of M3 to M5.

## Method

- **Now:** `make bench` (`bench/bench.c`). All inputs are loaded into memory first and every run happens in one process, so timings exclude disk and process start-up. Each figure is the best of 5 runs (the naive and unlimited matchers: 1 run). Release build. CPU, compiler and flags are recorded with every table.
- **Corpora:** Canterbury (2.8 MB) and Canterbury large (11 MB), downloaded and SHA-1-verified by `bench/fetch-corpus.sh`. Silesia (about 200 MB) is planned for the v1.0 comparison.
- **Planned comparisons:** `gzip -6`, `bzip2`, `xz`, `zstd -3` on the same machine; peak memory (`/usr/bin/time -v`); from M9, speedup and ratio loss per thread count.

The goal is not to beat these tools but to explain why tcomp is faster or slower and compresses better or worse.
