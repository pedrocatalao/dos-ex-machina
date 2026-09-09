# Generated data

Everything in this directory is machine output, committed so that a plain
`cmake && make` needs no Python and works offline. Nothing here is edited
by hand; a change is a change to the source in `assets/` or to the tool,
followed by `tools/regen.sh`. CI runs `tools/regen.sh --check` and fails
if a reproducible file drifts from what its tool produces.

| File | What | Tool | Source | Status |
|---|---|---|---|---|
| `splash.c`, `splash.h` | startup splash, RLE | `mksplash.py`, width 1024 | `assets/splash-src.png` | reproducible |
| `mark.h` | the DXM mark engraved on the case | `mklogo.py`, width 320 | `assets/dxm-badge.png` | reproducible |
| `font16.h` | the VGA 8x16 CP437 font the text screen draws with | `mkfont.py` | `assets/cp437-8x16.fnt.uu` (3) | reproducible |
| `logo.h` | the POST-screen wordmark | `mklogo.py` | `assets/logo-src.png` | frozen (1) |
| `icon.h` | window icon, 128 px | `mklogo.py` | `assets/icon-src.png` | frozen (1) |
| `road.h` | the road mark; navigator fallback art | `mklogo.py` | `assets/road-src.png` | frozen (1) |
| `corner_sticker.h` | the corner sticker on the case | `mklogo.py` | `assets/corner-sticker.png` | frozen (1) |
| `sb_logo.h` | Sound Blaster sticker | `mklogo.py` | not in the repository | frozen (2) |
| `fdd_pcm.h` | floppy seek sound, 22050 Hz s16 | `mkpcm.py` | not in the repository | frozen (2) |

(1) Made by an earlier `mklogo.py` that scaled differently; the current
tool does not reproduce these bytes from the same source. Regenerating
them changes the picture, so that is a visual decision to take on its own,
with the golden frames re-blessed, not a side effect of a rebuild.

(2) The source file was never committed. Recover it or replace it, then
add the line to `regen.sh`.

(3) FreeBSD's `share/syscons/fonts/cp437-8x16.fnt`, as it sits in their
tree (uuencoded), under the BSD licence. Glyph for glyph it is the IBM
VGA ROM font - byte-identical to the one DOSBox draws text mode with, which
is the point: the machine's own text and a real DOS's are the same glyphs.
