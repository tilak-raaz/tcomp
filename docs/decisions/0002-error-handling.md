# 0002: Status codes, and only main.c may print or exit

- **Date:** 2026-10-05
- **Milestone:** M0
- **Status:** Accepted

## Context

A decompressor reads untrusted files. Bad input is normal, not exceptional, and must be reported without crashing. The library will later be called from tests, fuzzers and worker threads, none of which can survive a stray `exit()`.

## Options considered

1. **Print and `exit()` on error inside library code.** Simple, but untestable and fatal inside a fuzzer or thread.
2. **Return `-1` and set a global error variable.** Familiar from libc, but globals are not thread-safe without extra work.
3. **Return an enum status from every fallible function.** Explicit, thread-safe, easy to test.

## Decision

Every fallible function returns `tcomp_status`. `TCOMP_OK` is zero so `if (st)` reads as "failed". `tcomp_strerror()` maps codes to messages. Only `src/main.c` prints and chooses exit codes: 0 success, 1 runtime error, 2 usage error. On failure the CLI deletes any partial output file.

## Consequences

- Tests can assert the exact error for each kind of bad input.
- Fuzzing (M8) can call the decoder directly in a loop.
- Every call site must check the return value; reviews should look for ignored statuses.
