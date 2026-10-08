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
head -c 16384 /dev/urandom > "$DATA/random_16KiB.bin"   # LZ77 worst case, small enough to be quick
printf 'héllo wörld — ñandú 日本語 😀\n%.0s' $(seq 1 500) > "$DATA/utf8.txt"
cat src/*.c include/tcomp/*.h > "$DATA/source_code.txt"
cp "$TCOMP" "$DATA/executable.bin"
# Starts with our own magic: must still roundtrip as ordinary data.
printf 'TCMP\0\0looks like a header' > "$DATA/fake_header.bin"
# Every byte value once.
for i in $(seq 0 255); do printf "\\$(printf '%03o' "$i")"; done > "$DATA/all_bytes.bin"

size_of()   { wc -c < "$1" | tr -d ' '; }
method_of() { od -An -tu1 -j5 -N1 "$1" | tr -d ' '; }  # header byte 5

# ---- positive tests: compress -> decompress -> compare --------------------
for f in "$DATA"/*; do
    name=$(basename "$f")

    for m in auto store huffman lz77; do
        # The naive LZ77 matcher needs minutes for 1 MiB of random data under
        # the sanitizers; random_16KiB covers that case. Remove in M6.
        if [ "$m" = lz77 ] && [ "$name" = random_1MiB.bin ]; then
            continue
        fi
        "$TCOMP" compress -m "$m" "$f" "$OUT/$name.$m.tcmp" \
            && "$TCOMP" decompress "$OUT/$name.$m.tcmp" "$OUT/$name.restored" \
            && cmp -s "$f" "$OUT/$name.restored" \
            && ok "files/$m: $name" || bad "files/$m: $name"
    done

    "$TCOMP" compress - - < "$f" | "$TCOMP" decompress - - | cmp -s "$f" - \
        && ok "pipes: $name" || bad "pipes: $name"

    # auto must never be worse than storing (input + 6-byte header).
    [ "$(size_of "$OUT/$name.auto.tcmp")" -le $(( $(size_of "$f") + 6 )) ] \
        && ok "auto never expands: $name" || bad "auto never expands: $name"
done

# ---- method choice --------------------------------------------------------
[ "$(method_of "$OUT/random_1MiB.bin.auto.tcmp")" = 0 ] \
    && ok "auto stores random data" || bad "auto stores random data"
[ "$(method_of "$OUT/source_code.txt.auto.tcmp")" = 1 ] \
    && ok "auto uses huffman on text" || bad "auto uses huffman on text"
[ "$(size_of "$OUT/source_code.txt.huffman.tcmp")" -lt $(( $(size_of "$DATA/source_code.txt") * 7 / 10 )) ] \
    && ok "huffman shrinks text below 70%" || bad "huffman shrinks text below 70%"
[ "$(size_of "$OUT/zeros_1MiB.bin.lz77.tcmp")" -lt 20000 ] \
    && ok "lz77 shrinks 1 MiB of zeros below 20 KB" || bad "lz77 shrinks 1 MiB of zeros below 20 KB"
[ "$(size_of "$OUT/source_code.txt.lz77.tcmp")" -lt "$(size_of "$OUT/source_code.txt.huffman.tcmp")" ] \
    && ok "lz77 beats huffman on source code" || bad "lz77 beats huffman on source code"

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

GOOD="$OUT/source_code.txt.huffman.tcmp"
head -c 100 "$GOOD" > "$OUT/h_table.tcmp";       expect_fail "huffman: cut in code table" "$OUT/h_table.tcmp"
head -c -50 "$GOOD" > "$OUT/h_data.tcmp";        expect_fail "huffman: cut in data"       "$OUT/h_data.tcmp"
{ cat "$GOOD"; printf 'X'; } > "$OUT/h_tail.tcmp"; expect_fail "huffman: trailing garbage" "$OUT/h_tail.tcmp"
# Every code length 1: an impossible (oversubscribed) code.
{ printf 'TCMP\0\001\001\0\0\0\0\0\0\0'; head -c 128 /dev/zero | tr '\0' '\021'; printf '\0'; } > "$OUT/h_lens.tcmp"
expect_fail "huffman: impossible code lengths" "$OUT/h_lens.tcmp"

GOOD="$OUT/source_code.txt.lz77.tcmp"
head -c -20 "$GOOD" > "$OUT/l_data.tcmp";          expect_fail "lz77: cut in data"       "$OUT/l_data.tcmp"
{ cat "$GOOD"; printf 'X'; } > "$OUT/l_tail.tcmp"; expect_fail "lz77: trailing garbage" "$OUT/l_tail.tcmp"
# size 3, then a match (flag 1) before any literal: reaches before the start.
printf 'TCMP\0\002\003\0\0\0\0\0\0\0\200\0\0' > "$OUT/l_early.tcmp"
expect_fail "lz77: match before any output" "$OUT/l_early.tcmp"

# ---- CLI usage ------------------------------------------------------------
"$TCOMP" > /dev/null 2>&1;            [ $? -eq 2 ] && ok "usage: no args exits 2" || bad "usage: no args exits 2"
"$TCOMP" frobnicate a b > /dev/null 2>&1; [ $? -eq 2 ] && ok "usage: bad command exits 2" || bad "usage: bad command exits 2"
"$TCOMP" compress -m bogus a b > /dev/null 2>&1; [ $? -eq 2 ] && ok "usage: unknown method exits 2" || bad "usage: unknown method exits 2"
"$TCOMP" compress -m huffman > /dev/null 2>&1; [ $? -eq 2 ] && ok "usage: -m without files exits 2" || bad "usage: -m without files exits 2"
"$TCOMP" --version | grep -q '^tcomp ' && ok "usage: --version" || bad "usage: --version"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
