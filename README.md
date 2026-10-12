# tcomp

A lossless file compressor written from scratch in C11.

tcomp is being built milestone by milestone toward an LZ77 + canonical Huffman compressor (the same family as gzip/DEFLATE) with its own checksummed `.tcmp` file format, streaming I/O, multithreaded block compression, a benchmark suite and fuzz testing. See [docs/ROADMAP.md](docs/ROADMAP.md) for the plan and current status.

> **Status: M4 (hash chains) complete.** tcomp picks the best of canonical Huffman coding and LZ77 for each file (2.93× on the 14 MB Canterbury benchmark set). A hash-chain matcher makes LZ77 205× faster than the naive version. M5 combines LZ77 and Huffman, DEFLATE-style.

## Build

Requirements: a C11 compiler (gcc or clang), GNU make, and a POSIX system. Development and CI use Linux.

```sh
make                  # debug build with AddressSanitizer + UBSan -> build/debug/tcomp
make BUILD=release    # optimised build -> build/release/tcomp
make test             # unit tests + roundtrip tests
make CC=clang test    # same with clang
make corpus           # round-trip the Canterbury corpus and print ratios
make bench            # speed and ratio benchmark (release build, ~4.5 min)
make bench-quick      # same without the slow naive matcher (~25 s)
```

On Ubuntu/Debian, install everything with:

```sh
sudo apt install build-essential clang clang-format gdb valgrind
sudo apt install "libclang-rt-$(clang -dumpversion | cut -d. -f1)-dev"   # sanitizers for clang
```

## Usage

```sh
tcomp compress   input.txt  output.tcmp     # picks the smallest method
tcomp compress -m lz77 input.txt out.tcmp     # force a method: auto | huffman | lz77 | store
tcomp decompress output.tcmp restored.txt     # method is read from the file
tcomp --version

# stdin/stdout with -
cat input.txt | tcomp compress - - | tcomp decompress - - > restored.txt
```

Exit codes: `0` success, `1` runtime error (bad input, I/O failure), `2` usage error. A failed decompression never leaves a partial output file behind.

## Project layout

```
include/tcomp/   public headers; every function documented
src/             library code; main.c is the CLI and the only file that prints or exits
tests/unit/      C unit tests (tiny framework in test.h)
tests/           roundtrip.sh: end-to-end CLI tests; corpus.sh: Canterbury corpus check
bench/           bench.c: benchmark harness; fetch-corpus.sh: downloads and verifies benchmark data
docs/            roadmap, format spec, design, benchmarks, decision notes
```

## Documentation

| Document | What it covers |
| --- | --- |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Milestones M0–M10 and progress |
| [docs/FORMAT.md](docs/FORMAT.md) | Byte-level specification of `.tcmp` files |
| [docs/DESIGN.md](docs/DESIGN.md) | Architecture, modules, conventions |
| [docs/BENCHMARKS.md](docs/BENCHMARKS.md) | Results with analysis, and methodology |
| [docs/decisions/](docs/decisions/) | Short notes explaining each significant design decision |

## Benchmarks

14 files, 13,970,266 bytes (Canterbury + Canterbury large), one thread, Intel Xeon @ 2.80 GHz:

| | Ratio | Compress | Decompress |
| --- | ---: | ---: | ---: |
| Huffman | 2.24× | 124 MB/s | 88 MB/s |
| LZ77, hash chains (default) | 2.44× | 13.2 MB/s | 167 MB/s |
| auto (best per file) | **2.93×** | 13.5 MB/s | 136 MB/s |

The hash-chain matcher is 205× faster than the naive O(n × window) matcher it replaced, and 57× faster with byte-identical output when its chain is unlimited. Full tables and analysis in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

## License

MIT. See [LICENSE](LICENSE).
