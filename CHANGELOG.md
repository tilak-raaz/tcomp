# Changelog

All notable changes, one section per milestone tag. Newest first.

## v0.0-setup (M0): 2026-10-05

### Added
- Makefile with debug (ASan + UBSan) and release builds, `test`, `format` and `format-check` targets.
- `.tcmp` container version 0 with the STORE method ([docs/FORMAT.md](docs/FORMAT.md)).
- CLI: `compress`, `decompress`, `--version`, `--help`, `-` for stdin/stdout.
- Unit test framework and 13 unit tests.
- Roundtrip script: 11 edge-case files through files and pipes, plus negative tests for invalid input.
- GitHub Actions CI: gcc and clang × debug and release, plus a formatting check.
- Docs: README, ROADMAP, FORMAT, DESIGN, BENCHMARKS, decision notes 0001 and 0002.
