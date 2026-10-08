# Benchmarks

Ratios are tracked from M2. Speed and memory benchmarking starts in M6.

## M2: order-0 Huffman on the Canterbury corpus

Measured 2026-10-06 with `tcomp compress -m huffman`, gcc 13.3, `-O2`. Reproduce the tcomp column with `make corpus`.

- **H0 bound:** the order-0 entropy of the file, ⌈n·H/8⌉ bytes. No coder that treats bytes independently can beat it.
- **over:** how far tcomp's output, including its 142-byte header, is above that bound.
- **gzip -9:** for context only. gzip also uses LZ77, which tcomp gets in M3–M4.

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
- **gzip is 1.8× smaller overall** because LZ77 removes repeated strings, which an order-0 model cannot see. Closing that gap is the point of M3 and M4.

## Method (from M6)

- **Corpora:** Canterbury (quick checks) and Silesia (main benchmark, about 200 MB of mixed real-world data), plus random and already-compressed files.
- **Compared against:** `gzip -6`, `bzip2`, `xz`, `zstd -3`.
- **Measured:** compression ratio, compression MB/s, decompression MB/s, peak memory (`/usr/bin/time -v`), and from M9 speedup and ratio loss per thread count.
- **Fairness:** same machine, release build, best of 5 runs, input files warm in the page cache. CPU, OS, compiler and flags are recorded with every result.

The goal is not to beat these tools but to explain why tcomp is faster or slower and compresses better or worse.
