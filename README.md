# tcomp

A lossless file compressor written from scratch in C11.

tcomp is being built milestone by milestone toward an LZ77 + canonical Huffman compressor (the same family as gzip/DEFLATE) with its own checksummed `.tcmp` file format, streaming I/O, multithreaded block compression, a benchmark suite and fuzz testing. See [docs/ROADMAP.md](docs/ROADMAP.md) for the plan and current status.

> **Status: M2 (Huffman) complete.** tcomp compresses with canonical Huffman coding, within about 1% of the order-0 entropy bound on large text files (2.14× on the Canterbury corpus overall). LZ77 arrives in M3.

## Build

Requirements: a C11 compiler (gcc or clang), GNU make, and a POSIX system. Development and CI use Linux.

```sh
make                  # debug build with AddressSanitizer + UBSan -> build/debug/tcomp
make BUILD=release    # optimised build -> build/release/tcomp
make test             # unit tests + roundtrip tests
make CC=clang test    # same with clang
make corpus           # round-trip the Canterbury corpus and print ratios
```

On Ubuntu/Debian, install everything with:

```sh
sudo apt install build-essential clang clang-format gdb valgrind
sudo apt install "libclang-rt-$(clang -dumpversion | cut -d. -f1)-dev"   # sanitizers for clang
```

## Usage

```sh
tcomp compress   input.txt  output.tcmp     # picks the smallest method
tcomp compress -m huffman input.txt out.tcmp  # force a method: auto | huffman | store
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
bench/           fetch-corpus.sh: downloads and verifies benchmark data (not committed)
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

Canterbury corpus, order-0 Huffman (M2): **2,810,784 → 1,313,970 bytes (2.14×)**, within 3.1% of the order-0 entropy bound overall and about 1% on large text files. Per-file results and analysis in [docs/BENCHMARKS.md](docs/BENCHMARKS.md). Speed benchmarks against gzip, bzip2, xz and zstd start in M6.

## License

MIT. See [LICENSE](LICENSE).
