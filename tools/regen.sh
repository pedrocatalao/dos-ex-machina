#!/bin/sh
# Regenerate the baked data under src/gen/ from the sources in assets/.
#
#   tools/regen.sh          rewrite the reproducible files in place
#   tools/regen.sh --check  regenerate into a scratch directory and fail if
#                           anything differs from what is committed (CI)
#
# Only files whose source and tool state are in the repository are handled
# here; the rest are frozen and listed in src/gen/README.md.  Adding a
# generated file means adding its line below and its source to assets/.
set -eu
cd "$(dirname "$0")/.."

PY=${PYTHON:-python3}
mode=${1:-write}
if [ "$mode" = "--check" ]; then
    out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
else
    out=src/gen
fi

#  source                  symbol       output                      width
$PY tools/mklogo.py  assets/dxm-badge.png   dxm_mark   "$out/mark.h"   320
$PY tools/mksplash.py assets/splash-src.png dxm_splash "$out/splash.c" "$out/splash.h" 1024
$PY tools/mkfont.py   assets/cp437-8x16.fnt.uu dxm_font16 "$out/font16.h"
$PY tools/mklogo.py  assets/multimedia-sticker.png dxm_multimedia "$out/multimedia.h"
$PY tools/mkbanners.py "$out/banners.c" "$out/banners.h" assets/banners/*.jpg

if [ "$mode" = "--check" ]; then
    rc=0
    for f in mark.h splash.c splash.h font16.h multimedia.h banners.c banners.h; do
        if ! cmp -s "$out/$f" "src/gen/$f"; then
            echo "src/gen/$f is not what tools/regen.sh produces" >&2; rc=1
        fi
    done
    [ $rc -eq 0 ] && echo "src/gen: reproducible files match"
    exit $rc
fi
echo "src/gen: regenerated mark.h splash.c splash.h font16.h multimedia.h banners.c banners.h"
