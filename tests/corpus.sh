#!/usr/bin/env bash
# Round-trip every Canterbury corpus file through tcomp and print the ratios.
#
#   usage: tests/corpus.sh <path-to-tcomp> <scratch-dir>
#
# Run via `make corpus`, which downloads and verifies the corpus first.
# Fails if any file does not decompress to exactly the original. gzip -9 is
# shown for reference when gzip is installed.

set -u

TCOMP=${1:?usage: corpus.sh <tcomp> <scratch-dir>}
WORK=${2:?usage: corpus.sh <tcomp> <scratch-dir>}
CORPUS=bench/corpus/canterbury
METHODS="huffman lz77 lzh auto"

export ASAN_OPTIONS="exitcode=86:detect_leaks=1"
export UBSAN_OPTIONS="halt_on_error=1:exitcode=86:print_stacktrace=1"

rm -rf "$WORK"
mkdir -p "$WORK"

size_of() { wc -c < "$1" | tr -d ' '; }
have_gzip=0
command -v gzip > /dev/null && have_gzip=1

fail=0
declare -A total
total[in]=0
for m in $METHODS gzip; do total[$m]=0; done

printf '%-14s %10s' file original
for m in $METHODS; do printf ' %8s' "$m"; done
[ "$have_gzip" -eq 1 ] && printf ' %8s' 'gzip -9'
printf '   (ratios)\n'

row() { # name in sizes...
    local name=$1 in=$2
    shift 2
    printf '%-14s %10d' "$name" "$in"
    for s in "$@"; do awk -v a="$in" -v b="$s" 'BEGIN { printf " %7.2fx", a / b }'; done
    printf '\n'
}

for f in "$CORPUS"/*; do
    name=$(basename "$f")
    [ "$name" = SHA1SUM ] && continue

    sizes=()
    for m in $METHODS; do
        if ! { "$TCOMP" compress -m "$m" "$f" "$WORK/$name.$m.tcmp" \
               && "$TCOMP" decompress "$WORK/$name.$m.tcmp" "$WORK/$name.out" \
               && cmp -s "$f" "$WORK/$name.out"; }; then
            printf 'FAIL %s (%s)\n' "$name" "$m" >&2
            fail=$((fail + 1))
        fi
        s=$(size_of "$WORK/$name.$m.tcmp")
        sizes+=("$s")
        total[$m]=$((total[$m] + s))
    done
    if [ "$have_gzip" -eq 1 ]; then
        s=$(gzip -9 -c "$f" | wc -c | tr -d ' ')
        sizes+=("$s")
        total[gzip]=$((total[gzip] + s))
    fi

    in=$(size_of "$f")
    total[in]=$((total[in] + in))
    row "$name" "$in" "${sizes[@]}"
done

totals=()
for m in $METHODS; do totals+=("${total[$m]}"); done
[ "$have_gzip" -eq 1 ] && totals+=("${total[gzip]}")
row TOTAL "${total[in]}" "${totals[@]}"

echo
if [ "$fail" -eq 0 ]; then
    echo "corpus: all files round-trip"
else
    echo "corpus: $fail failures" >&2
fi
[ "$fail" -eq 0 ]
