#!/bin/bash
# Append the thumbnail view strings to Wine's po files.
# Appending keeps this independent of po file churn: wrc matches by msgid,
# and entries Wine already translates itself are left alone.
# usage: apply-translations.sh <wine source tree>
set -euo pipefail
wine_src=$(realpath "$1")
table="$(dirname "$(realpath "$0")")/../translations/thumbnails.tsv"

while IFS=$'\t' read -r lang msgid msgstr; do
    [[ -z "$lang" || "$lang" == \#* ]] && continue
    po="$wine_src/po/$lang.po"
    [ -f "$po" ] || continue
    grep -qxF "msgid \"$msgid\"" "$po" && continue
    printf '\nmsgid "%s"\nmsgstr "%s"\n' "$msgid" "$msgstr" >> "$po"
done < "$table"
