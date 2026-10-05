# Benchmarks

No results yet. Benchmarking starts in M6.

## Method (planned)

- **Corpora:** Canterbury (quick checks) and Silesia (main benchmark, about 200 MB of mixed real-world data), plus random and already-compressed files.
- **Compared against:** `gzip -6`, `bzip2`, `xz`, `zstd -3`.
- **Measured:** compression ratio, compression MB/s, decompression MB/s, peak memory (`/usr/bin/time -v`), and from M9 speedup and ratio loss per thread count.
- **Fairness:** same machine, release build, best of 5 runs, input files warm in the page cache. CPU, OS, compiler and flags are recorded with every result.

The goal is not to beat these tools but to explain why tcomp is faster or slower and compresses better or worse.
