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
# 4. Checks the golden files in tests/fixtures/v1 (written by tcomp 1.0)
#    still decode, and that the encoder still produces them byte for byte.
# 5. Exercises the info and verify commands and CLI usage errors.
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
head -c 16384 /dev/urandom > "$DATA/random_16KiB.bin"
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

    for m in auto store huffman lz77 lzh; do
        "$TCOMP" compress -m "$m" "$f" "$OUT/$name.$m.tcmp" \
            && "$TCOMP" decompress "$OUT/$name.$m.tcmp" "$OUT/$name.restored" \
            && cmp -s "$f" "$OUT/$name.restored" \
            && ok "files/$m: $name" || bad "files/$m: $name"
    done

    "$TCOMP" compress - - < "$f" | "$TCOMP" decompress - - | cmp -s "$f" - \
        && ok "pipes: $name" || bad "pipes: $name"

    # auto must never be worse than storing (input + 15-byte header + 4-byte trailer).
    [ "$(size_of "$OUT/$name.auto.tcmp")" -le $(( $(size_of "$f") + 19 )) ] \
        && ok "auto never expands: $name" || bad "auto never expands: $name"
done

# ---- method choice --------------------------------------------------------
[ "$(method_of "$OUT/random_1MiB.bin.auto.tcmp")" = 0 ] \
    && ok "auto stores random data" || bad "auto stores random data"
[ "$(method_of "$OUT/source_code.txt.auto.tcmp")" = 3 ] \
    && ok "auto uses lzh on source code" || bad "auto uses lzh on source code"
[ "$(size_of "$OUT/source_code.txt.huffman.tcmp")" -lt $(( $(size_of "$DATA/source_code.txt") * 7 / 10 )) ] \
    && ok "huffman shrinks text below 70%" || bad "huffman shrinks text below 70%"
[ "$(size_of "$OUT/zeros_1MiB.bin.lz77.tcmp")" -lt 20000 ] \
    && ok "lz77 shrinks 1 MiB of zeros below 20 KB" || bad "lz77 shrinks 1 MiB of zeros below 20 KB"
[ "$(size_of "$OUT/source_code.txt.lz77.tcmp")" -lt "$(size_of "$OUT/source_code.txt.huffman.tcmp")" ] \
    && ok "lz77 beats huffman on source code" || bad "lz77 beats huffman on source code"
[ "$(size_of "$OUT/source_code.txt.lzh.tcmp")" -lt "$(size_of "$OUT/source_code.txt.lz77.tcmp")" ] \
    && ok "lzh beats lz77 on source code" || bad "lzh beats lz77 on source code"

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
printf 'TCMP\001\310' > "$OUT/method.tcmp";  expect_fail "unknown method"   "$OUT/method.tcmp"
printf 'TCMP\0\0hello' > "$OUT/v0.tcmp";     expect_fail "format version 0" "$OUT/v0.tcmp"
printf 'TCMP\001\0\0\005\0' > "$OUT/hdr.tcmp"; expect_fail "header cut in size field" "$OUT/hdr.tcmp"
expect_fail "missing input file" "$OUT/does_not_exist.tcmp"

GOOD="$OUT/source_code.txt.huffman.tcmp"
head -c 100 "$GOOD" > "$OUT/h_table.tcmp";       expect_fail "huffman: cut in code table" "$OUT/h_table.tcmp"
head -c -50 "$GOOD" > "$OUT/h_data.tcmp";        expect_fail "huffman: cut in data"       "$OUT/h_data.tcmp"
{ cat "$GOOD"; printf 'X'; } > "$OUT/h_tail.tcmp"; expect_fail "huffman: trailing garbage" "$OUT/h_tail.tcmp"
# Every code length 1: an impossible (oversubscribed) code. (Header: version 1,
# HUFFMAN, flags 0, size 1; then 128 bytes of lengths, 1 data byte, 4 CRC bytes.)
{ printf 'TCMP\001\001\0\001\0\0\0\0\0\0\0'; head -c 128 /dev/zero | tr '\0' '\021'; printf '\0\0\0\0\0'; } > "$OUT/h_lens.tcmp"
expect_fail "huffman: impossible code lengths" "$OUT/h_lens.tcmp"

GOOD="$OUT/source_code.txt.lz77.tcmp"
head -c -20 "$GOOD" > "$OUT/l_data.tcmp";          expect_fail "lz77: cut in data"       "$OUT/l_data.tcmp"
{ cat "$GOOD"; printf 'X'; } > "$OUT/l_tail.tcmp"; expect_fail "lz77: trailing garbage" "$OUT/l_tail.tcmp"
GOOD="$OUT/source_code.txt.lzh.tcmp"
head -c 30 "$GOOD" > "$OUT/z_head.tcmp";           expect_fail "lzh: cut in code tables" "$OUT/z_head.tcmp"
head -c -20 "$GOOD" > "$OUT/z_data.tcmp";          expect_fail "lzh: cut in data"        "$OUT/z_data.tcmp"
{ cat "$GOOD"; printf 'X'; } > "$OUT/z_tail.tcmp"; expect_fail "lzh: trailing garbage"   "$OUT/z_tail.tcmp"

# size 3, then a match (flag 1) before any literal: reaches before the start.
printf 'TCMP\001\002\0\003\0\0\0\0\0\0\0\200\0\0\0\0\0\0' > "$OUT/l_early.tcmp"
expect_fail "lz77: match before any output" "$OUT/l_early.tcmp"

# ---- checksum: damage that decodes cleanly is still caught -----------------
# A STORE payload has no structure, so only the CRC can notice a changed byte.
GOOD="$OUT/source_code.txt.store.tcmp"
cp "$GOOD" "$OUT/c_store.tcmp"
printf 'Z' | dd of="$OUT/c_store.tcmp" bs=1 seek=1000 conv=notrunc 2> /dev/null
expect_fail "store: one byte changed" "$OUT/c_store.tcmp"
grep -q "checksum mismatch" "$OUT/stderr.txt" \
    && ok "store: reports checksum mismatch" || bad "store: reports checksum mismatch"
for m in huffman lz77 lzh; do
    GOOD="$OUT/source_code.txt.$m.tcmp"
    n=$(size_of "$GOOD")
    { head -c $((n - 4)) "$GOOD"; printf 'ABCD'; } > "$OUT/c_crc.tcmp"
    expect_fail "$m: wrong CRC in trailer" "$OUT/c_crc.tcmp"
done
head -c -1 "$OUT/source_code.txt.store.tcmp" > "$OUT/s_cut.tcmp"
expect_fail "store: trailer cut" "$OUT/s_cut.tcmp"

# ---- golden files: format stability --------------------------------------
FIX=tests/fixtures
for f in "$FIX"/v1/sample.*.tcmp; do
    m=$(basename "$f" .tcmp); m=${m#sample.}
    "$TCOMP" decompress "$f" "$OUT/golden.out" && cmp -s "$OUT/golden.out" "$FIX/sample.txt" \
        && ok "golden: decodes v1 $m file" || bad "golden: decodes v1 $m file"
    "$TCOMP" compress -m "$m" "$FIX/sample.txt" "$OUT/golden.tcmp" && cmp -s "$OUT/golden.tcmp" "$f" \
        && ok "golden: encoder output unchanged for $m" \
        || bad "golden: encoder output unchanged for $m (if intended, regenerate tests/fixtures/v1)"
done
"$TCOMP" decompress "$FIX/v1/empty.tcmp" "$OUT/golden.out" && [ ! -s "$OUT/golden.out" ] \
    && ok "golden: decodes empty file" || bad "golden: decodes empty file"

# ---- info and verify -----------------------------------------------------
info=$("$TCOMP" info "$OUT/source_code.txt.auto.tcmp")
echo "$info" | grep -q "^method *lzh$" && echo "$info" | grep -q "^format *tcomp v1$" \
    && ok "info: method and format" || bad "info: method and format"
want=$(size_of "$DATA/source_code.txt" | sed -e :a -e 's/\B[0-9]\{3\}\>/,&/;ta')
echo "$info" | grep -q "^original *$want bytes$" \
    && ok "info: original size" || bad "info: original size ($want)"
"$TCOMP" info - < "$FIX/v1/sample.lzh.tcmp" | grep -q "^crc32 *96b2047b$" \
    && ok "info: stdin and crc32" || bad "info: stdin and crc32"
"$TCOMP" info "$FIX/v1/empty.tcmp" > /dev/null && ok "info: empty file" || bad "info: empty file"
"$TCOMP" info "$DATA/source_code.txt" > /dev/null 2>&1; [ $? -eq 1 ] \
    && ok "info: rejects plain text" || bad "info: rejects plain text"

for f in "$FIX"/v1/*.tcmp; do
    "$TCOMP" verify "$f" > /dev/null || bad "verify: $(basename "$f")"
done && ok "verify: golden files"
"$TCOMP" verify - < "$OUT/source_code.txt.lzh.tcmp" | grep -q ": OK$" \
    && ok "verify: stdin" || bad "verify: stdin"
"$TCOMP" verify "$OUT/c_store.tcmp" > /dev/null 2>&1; [ $? -eq 1 ] \
    && ok "verify: rejects damaged file" || bad "verify: rejects damaged file"

# ---- CLI usage ------------------------------------------------------------
"$TCOMP" > /dev/null 2>&1;            [ $? -eq 2 ] && ok "usage: no args exits 2" || bad "usage: no args exits 2"
"$TCOMP" frobnicate a b > /dev/null 2>&1; [ $? -eq 2 ] && ok "usage: bad command exits 2" || bad "usage: bad command exits 2"
"$TCOMP" compress -m bogus a b > /dev/null 2>&1; [ $? -eq 2 ] && ok "usage: unknown method exits 2" || bad "usage: unknown method exits 2"
"$TCOMP" compress -m huffman > /dev/null 2>&1; [ $? -eq 2 ] && ok "usage: -m without files exits 2" || bad "usage: -m without files exits 2"
"$TCOMP" info > /dev/null 2>&1;       [ $? -eq 2 ] && ok "usage: info without file exits 2" || bad "usage: info without file exits 2"
"$TCOMP" verify a b > /dev/null 2>&1; [ $? -eq 2 ] && ok "usage: verify with two files exits 2" || bad "usage: verify with two files exits 2"
"$TCOMP" --version | grep -q '^tcomp 1\.' && ok "usage: --version" || bad "usage: --version"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
