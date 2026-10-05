# 0001: C11, plain Make, sanitizers on by default

- **Date:** 2026-10-05
- **Milestone:** M0
- **Status:** Accepted

## Context

The project needs a language standard, a build system and a way to catch memory bugs early. Compression code is full of buffer indexing and bit shifts, which are exactly where C goes wrong silently.

## Options considered

1. **Build system: CMake.** Portable and common in industry, but adds a configuration layer that hides what the compiler is actually doing.
2. **Build system: plain GNU Make.** One readable file; every flag is visible. Enough for a project of this size.
3. **Memory checking: Valgrind only.** Thorough but slow and not available on Apple Silicon.
4. **Memory checking: ASan + UBSan in every debug build.** Fast enough to run on every test; also catches undefined behaviour such as signed overflow and oversized shifts.

## Decision

C11 with POSIX, plain GNU Make, and AddressSanitizer + UndefinedBehaviorSanitizer enabled in the default debug build. Warnings are errors (`-Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes`). CI builds with both gcc and clang in debug and release.

## Consequences

- Bugs in bit manipulation surface as test failures instead of silent corruption.
- Release builds are tested too, so optimisation-dependent bugs are caught.
- If the project ever needs Windows support, moving to CMake would be worth revisiting.
