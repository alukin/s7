#!/usr/bin/env bash
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

DOCS_DIR="${1:-.}"
OUT_DIR="${2:-md}"

mkdir -p "$OUT_DIR"

echo "Converting DOCX -> Markdown..."

find "$DOCS_DIR" -type f -iname "*.docx" | while read -r file; do
    base=$(basename "$file" .docx)

    echo "  $base.docx"

    pandoc \
        "$file" \
        -f docx \
        -t gfm \
        --wrap=none \
        --extract-media="$OUT_DIR/media/$base" \
        -o "$OUT_DIR/$base.md"
done

echo
echo "Done."
echo "Markdown files: $OUT_DIR"
