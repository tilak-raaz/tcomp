# Changelog

All notable changes, one section per milestone tag. Newest first.

## v0.1-bitio (M1): 2026-10-06

### Added
- Bit-level I/O module (`include/tcomp/bitio.h`, `src/bitio.c`): MSB-first bit writer with a growable buffer, and a bounds-checked bit reader with read, peek, skip and align.
- 16 bit I/O unit tests: exact expected bytes, 32-bit fields at every bit offset, truncation and invalid-argument cases, and 250,000 seeded random-width fields round-tripped.
- Decision note 0003: bit order and bit I/O design.

### Changed
- Unit tests split into one file per module (`tests/unit/test_<module>.c`) with a shared runner in `tests/unit/main.c`.
- Version string bumped to 0.1.0.

## v0.0-setup (M0): 2026-10-05

### Added
- Makefile with debug (ASan + UBSan) and release builds, `test`, `format` and `format-check` targets.
- `.tcmp` container version 0 with the STORE method ([docs/FORMAT.md](docs/FORMAT.md)).
- CLI: `compress`, `decompress`, `--version`, `--help`, `-` for stdin/stdout.
- Unit test framework and 13 unit tests.
- Roundtrip script: 11 edge-case files through files and pipes, plus negative tests for invalid input.
- GitHub Actions CI: gcc and clang × debug and release, plus a formatting check.
- Docs: README, ROADMAP, FORMAT, DESIGN, BENCHMARKS, decision notes 0001 and 0002.
