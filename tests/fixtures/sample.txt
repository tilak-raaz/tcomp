# tcomp

A lossless file compressor written from scratch in C11.

tcomp is being built milestone by milestone toward an LZ77 + canonical Huffman compressor (the same family as gzip/DEFLATE) with its own checksummed `.tcmp` file format, streaming I/O, multithreaded block compression, a benchmark suite and fuzz testing. See [docs/ROADMAP.md](docs/ROADMAP.md) for the plan and current status.

> **Status: M5 (LZ77 + Huffman) complete.** tcomp compresses DEFLATE-style: LZ77 back-references from a hash-chain matcher, Huffman-coded with two alphabets. **3.58× on the Canterbury corpus**, within 7% of gzip -9. Next: the v1.0 file format with CRC32 (M6).

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
tcomp compress -m lzh input.txt out.tcmp      # force a method: auto | lzh | lz77 | huffman | store
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

Canterbury corpus (11 files, 2,810,784 bytes):

| | Size | Ratio |
| --- | ---: | ---: |
| Huffman | 1,313,970 | 2.14× |
| LZ77 | 1,063,157 | 2.64× |
| **LZH (LZ77 + Huffman)** | **785,259** | **3.58×** |
| gzip -6 / -9 | 733,769 / 730,732 | 3.83× / 3.85× |

Speed on 14 files, 13,970,266 bytes (Canterbury + Canterbury large), one thread, Intel Xeon @ 2.80 GHz: LZH compresses at about 11 MB/s and decompresses at about 110 MB/s. The hash-chain matcher is 205× faster than the naive matcher it replaced. Full tables and analysis in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

## License

MIT. See [LICENSE](LICENSE).
