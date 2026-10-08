#!/usr/bin/env bash
# Download the Canterbury corpus into bench/corpus/ (git-ignored) and verify it.
#
# The corpus (https://corpus.canterbury.ac.nz) is the standard small benchmark
# for lossless compressors: 11 files, 2.8 MB, mixing English text, HTML, C and
# Lisp source, a man page, a spreadsheet, a fax image and a SPARC binary.
#
# It is fetched from a git mirror pinned to an exact commit, and every file is
# checked against the SHA-1 below (file sizes match the official corpus).
# Run again at any time: it does nothing if the corpus is already verified.

set -euo pipefail

REPO=https://github.com/pfalcon/canterbury-corpus.git
COMMIT=c380ef9dc1dc62691757fdfeebdc2f85710e1e46
DEST=bench/corpus

if [ -f "$DEST/.verified-$COMMIT" ]; then
    exit 0
fi

echo "Fetching Canterbury corpus @ ${COMMIT:0:12} ..."
rm -rf "$DEST"
git init -q "$DEST"
git -C "$DEST" fetch -q --depth 1 "$REPO" "$COMMIT"
git -C "$DEST" checkout -q FETCH_HEAD

(cd "$DEST/canterbury" && sha1sum --quiet -c -) <<'EOF'
37a087d23c8709e97aa45ece662faf3d07006a58  alice29.txt
fb7db2d0c1ba0a1be26fe1892a7f83bf01153770  asyoulik.txt
fc4c10407efe47f40eee55eba9bddffbe5948cf4  cp.html
31999f829d313a6c10314deba2854b23481ab346  fields.c
12bf64bf1d4c1f1119bea24e7bebd3167389220d  grammar.lsp
bb3c73adde28228f9a311ddfee8f76aeccf83c4b  kennedy.xls
77331951a4e24cd6d6315aa41b2cbdda882f685d  lcet10.txt
4575958b534bbe6e9d461b0b390300f54a5210cd  plrabn12.txt
96f7ab3d975ea4d823cf30be7dad5827f45858e9  ptt5
076fb264487f2d6868e67919e1949dcb4560e423  sum
777250a5ccf4fd95b48c1c9248ab82c2e0221913  xargs.1
EOF

touch "$DEST/.verified-$COMMIT"
echo "Canterbury corpus verified."
