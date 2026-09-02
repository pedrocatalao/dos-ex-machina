#!/bin/bash
# make_icns.sh <in.png> <out.icns> — build a macOS icon from a square PNG.
# macOS only: sips and iconutil are part of the OS.
set -euo pipefail
SRC="$1"; OUT="$2"
SET="$(mktemp -d)/icon.iconset"
mkdir -p "$SET"
# Every size macOS asks for.  The @2x entries are what a Retina Dock uses;
# omitting them leaves the icon visibly soft on any modern Mac.
for spec in "16 16x16" "32 16x16@2x" "32 32x32" "64 32x32@2x" \
            "128 128x128" "256 128x128@2x" "256 256x256" "512 256x256@2x" \
            "512 512x512" "1024 512x512@2x"; do
    px="${spec% *}"; name="${spec#* }"
    sips -s format png -z "$px" "$px" "$SRC" --out "$SET/icon_$name.png" >/dev/null
done
iconutil -c icns "$SET" -o "$OUT"
rm -rf "$(dirname "$SET")"
echo "wrote $OUT"
