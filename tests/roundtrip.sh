#!/usr/bin/env bash
# Roundtrip tests for the tcomp CLI.
#
#   usage: tests/roundtrip.sh <path-to-tcomp> <scratch-dir>
#
# 1. Generates a set of edge-case files.
# 2. For each: compress -> decompress -> byte-compare with the original,
#    once through file paths and once through stdin/stdout pipes.
# 3. Feeds invalid input to the decompressor and checks it fails cleanly
#    (exit code 1, no crash, no sanitizer report, no output file left behind).
#
# Sanitizer failures use exit code 86 so they can never be mistaken for a
# normal "bad input" error (exit code 1).

set -u

TCOMP=${1:?usage: roundtrip.sh <tcomp> <scratch-dir>}
WORK=${2:?usage: roundtrip.sh <tcomp> <scratch-dir>}

export ASAN_OPTIONS="exitcode=86:detect_leaks=1"
export UBSAN_OPTIONS="halt_on_error=1:exitcode=86:print_stacktrace=1"

DATA="$WORK/data"
OUT="$WORK/out"
rm -rf "$WORK"
mkdir -p "$DATA" "$OUT"

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf 'ok   %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1" >&2; }

# ---- generate test data ---------------------------------------------------
: > "$DATA/empty.bin"
printf 'x' > "$DATA/one_byte.txt"
printf '\0' > "$DATA/one_zero_byte.bin"
head -c 1048576 /dev/zero > "$DATA/zeros_1MiB.bin"
head -c 65537 /dev/zero | tr '\0' 'A' > "$DATA/repeated_A_64KiB_plus_1.txt"
head -c 1048576 /dev/urandom > "$DATA/random_1MiB.bin"
printf 'héllo wörld — ñandú 日本語 😀\n%.0s' $(seq 1 500) > "$DATA/utf8.txt"
cat src/*.c include/tcomp/*.h > "$DATA/source_code.txt"
cp "$TCOMP" "$DATA/executable.bin"
# Starts with our own magic: must still roundtrip as ordinary data.
printf 'TCMP\0\0looks like a header' > "$DATA/fake_header.bin"
# Every byte value once.
for i in $(seq 0 255); do printf "\\$(printf '%03o' "$i")"; done > "$DATA/all_bytes.bin"

# ---- positive tests: compress -> decompress -> compare --------------------
for f in "$DATA"/*; do
    name=$(basename "$f")

    "$TCOMP" compress "$f" "$OUT/$name.tcmp" \
        && "$TCOMP" decompress "$OUT/$name.tcmp" "$OUT/$name.restored" \
        && cmp -s "$f" "$OUT/$name.restored" \
        && ok "files: $name" || bad "files: $name"

    "$TCOMP" compress - - < "$f" | "$TCOMP" decompress - - | cmp -s "$f" - \
        && ok "pipes: $name" || bad "pipes: $name"
done

# ---- negative tests: bad input must fail cleanly --------------------------
# expect_fail <label> <file>: exit code must be exactly 1 and no output kept.
expect_fail() {
    local label=$1 input=$2 target="$OUT/should_not_exist"
    rm -f "$target"
    "$TCOMP" decompress "$input" "$target" 2> "$OUT/stderr.txt"
    local rc=$?
    if [ "$rc" -eq 1 ] && [ ! -e "$target" ]; then
        ok "rejects: $label"
    else
        bad "rejects: $label (exit $rc)"
        cat "$OUT/stderr.txt" >&2
    fi
}

expect_fail "empty file"        "$DATA/empty.bin"
expect_fail "plain text"        "$DATA/source_code.txt"
expect_fail "random bytes"      "$DATA/random_1MiB.bin"
printf 'TCM' > "$OUT/trunc.tcmp";            expect_fail "truncated header" "$OUT/trunc.tcmp"
printf 'TCMP\143\0' > "$OUT/ver.tcmp";       expect_fail "future version"   "$OUT/ver.tcmp"
printf 'TCMP\0\310' > "$OUT/method.tcmp";    expect_fail "unknown method"   "$OUT/method.tcmp"
expect_fail "missing input file" "$OUT/does_not_exist.tcmp"

# ---- CLI usage ------------------------------------------------------------
"$TCOMP" > /dev/null 2>&1;            [ $? -eq 2 ] && ok "usage: no args exits 2" || bad "usage: no args exits 2"
"$TCOMP" frobnicate a b > /dev/null 2>&1; [ $? -eq 2 ] && ok "usage: bad command exits 2" || bad "usage: bad command exits 2"
"$TCOMP" --version | grep -q '^tcomp ' && ok "usage: --version" || bad "usage: --version"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
