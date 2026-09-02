#!/bin/bash
# install.sh — put DOS ex Machina in your menu, for the current user only.
#
# Everything goes under ~/.local, so this needs no root and uninstalling is
# deleting what it lists.  Nothing on the system is touched and no shell
# configuration is edited: PATH is not modified, because a program that
# rewrites your shell profile behind your back is a program you cannot trust
# with anything else.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="$HOME/.local/share/dos-ex-machina"
APPS="$HOME/.local/share/applications"
ICONS="$HOME/.local/share/icons/hicolor"
BIN="$HOME/.local/bin"

echo "Installing to $DEST"
mkdir -p "$DEST" "$APPS" "$BIN"
# The whole directory, not just the binary: the executable finds SDL3 through
# an rpath of $ORIGIN/lib, so lib/ has to stay beside it.
cp -R "$HERE"/dxm "$HERE"/lib "$DEST"/ 2>/dev/null || {
    cp -R "$HERE"/* "$DEST"/ ; }
chmod +x "$DEST/dxm"

# A symlink is fine even though the rpath is relative: the loader resolves
# $ORIGIN against the REAL path, so it still finds $DEST/lib.
ln -sf "$DEST/dxm" "$BIN/dxm"

if [ -f "$HERE/dxm.png" ]; then
    for px in 32 64 128 256 512; do
        d="$ICONS/${px}x${px}/apps"
        mkdir -p "$d"
        if command -v magick >/dev/null 2>&1; then
            magick "$HERE/dxm.png" -resize "${px}x${px}" "$d/dxm.png"
        else
            cp "$HERE/dxm.png" "$d/dxm.png"    # unscaled; the theme copes
        fi
    done
fi

# Written here rather than shipped ready-made, because Exec has to be the
# absolute path of this particular install.
cat > "$APPS/dos-ex-machina.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=DOS ex Machina
Comment=A 1993 PC on your screen, running native ports of DOS games
Exec=$DEST/dxm
Icon=dxm
Terminal=false
Categories=Game;Emulator;
EOF

command -v update-desktop-database >/dev/null 2>&1 \
    && update-desktop-database "$APPS" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 \
    && gtk-update-icon-cache -f -t "$ICONS" 2>/dev/null || true

echo
echo "Done.  It should appear in your applications menu."
echo "From a terminal: $BIN/dxm"
case ":$PATH:" in
    *":$BIN:"*) echo "                (or just: dxm)" ;;
    *) echo "Note: $BIN is not on your PATH, so use the full path above." ;;
esac
echo
echo "To remove:  rm -rf '$DEST' '$BIN/dxm' '$APPS/dos-ex-machina.desktop'"
