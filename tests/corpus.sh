#!/usr/bin/env bash
# Round-trip every Canterbury corpus file through tcomp and print the ratios.
#
#   usage: tests/corpus.sh <path-to-tcomp> <scratch-dir>
#
# Run via `make corpus`, which downloads and verifies the corpus first.
# Fails if any file does not decompress to exactly the original.

set -u

TCOMP=${1:?usage: corpus.sh <tcomp> <scratch-dir>}
WORK=${2:?usage: corpus.sh <tcomp> <scratch-dir>}
CORPUS=bench/corpus/canterbury

export ASAN_OPTIONS="exitcode=86:detect_leaks=1"
export UBSAN_OPTIONS="halt_on_error=1:exitcode=86:print_stacktrace=1"

rm -rf "$WORK"
mkdir -p "$WORK"

size_of() { wc -c < "$1" | tr -d ' '; }

fail=0
total_in=0
total_huff=0
total_lz=0
row() { awk -v n="$1" -v a="$2" -v h="$3" -v l="$4" \
        'BEGIN { printf "%-14s %10d %10d %6.2fx %10d %6.2fx\n", n, a, h, a / h, l, a / l }'; }
printf '%-14s %10s %10s %7s %10s %7s\n' file original huffman ratio lz77 ratio
for f in "$CORPUS"/*; do
    name=$(basename "$f")
    [ "$name" = SHA1SUM ] && continue

    for m in huffman lz77 auto; do
        if ! { "$TCOMP" compress -m "$m" "$f" "$WORK/$name.$m.tcmp" \
               && "$TCOMP" decompress "$WORK/$name.$m.tcmp" "$WORK/$name.out" \
               && cmp -s "$f" "$WORK/$name.out"; }; then
            printf 'FAIL %s (%s)\n' "$name" "$m" >&2
            fail=$((fail + 1))
        fi
    done

    in=$(size_of "$f")
    huff=$(size_of "$WORK/$name.huffman.tcmp")
    lz=$(size_of "$WORK/$name.lz77.tcmp")
    total_in=$((total_in + in))
    total_huff=$((total_huff + huff))
    total_lz=$((total_lz + lz))
    row "$name" "$in" "$huff" "$lz"
done
row TOTAL "$total_in" "$total_huff" "$total_lz"

echo
if [ "$fail" -eq 0 ]; then
    echo "corpus: all files round-trip"
else
    echo "corpus: $fail failures" >&2
fi
[ "$fail" -eq 0 ]
