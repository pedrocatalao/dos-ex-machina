# DOS ex Machina

A retro PC on your screen: a procedurally-drawn beige case and CRT, booting a
simulated DOS prompt, from which you launch natively-ported DOS games. The
games run in the same tube, behind the same glass.

See [SPEC.md](SPEC.md) for the design and [PORTING.md](PORTING.md) for the
contract a game must satisfy to run here.

## Status — first working version (M1 + most of M2)

Working end to end on macOS: boot theater → `C:\>` → `SKYROADS` → the game
runs in the tube → Esc returns to the prompt → it relaunches.

- Procedural chassis, no raster art. Three layout variants solved from the
  host resolution (SPEC §6.3).
- Tube pipeline in one GL shader path: time-based phosphor persistence,
  barrel curvature, footprint-integrated beam/scanlines, aperture-grille mask
  pinned to output pixels, bloom, glass sheen, and coloured bezel spill.
- SkyRoads linked as an SDL-free static core. `nm -u libskyroads_core.a`
  reports 0 undefined `SDL_*` symbols.
- `--selftest` runs the conformance sequence from PORTING.md §5.

## Build

Needs SDL3 and a skyroads-sdl checkout on the `dxm-core` branch.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSKY_CORE=ON -DSKY_DXM_INCLUDE=$PWD/src -DDXM_SKYROADS=/path/to/skyroads-sdl
cmake --build build -j8
```

## Run

```bash
DXM_DATA=/path/to/skyroads/data ./build/dxm
```

Fullscreen is real fullscreen: the case fills the display edge to edge, with
no background around it. Extra width becomes more machine (wider bays, speaker
columns on ultrawide), never letterboxing.

Quit with `EXIT` at the prompt, Esc out of a game to return to `C:\>`, or
Cmd-Q at any time.

Dev flags (SPEC §11): `--windowed`, `--size WxH`, `--type CMD`,
`--shot out.bmp --frames N` (honours fullscreen), `--selftest`.

## Known gaps

- macOS only so far. `gpu.c` includes `<OpenGL/gl3.h>` directly; Linux and
  Windows need a GL loader (or the SDL_GPU port — SPEC §12.1).
- No `INSTALL` command yet; data dir comes from `DXM_DATA` (M3).
- Knobs are drawn but not yet interactive (SPEC §6.8).
- The Compact (<=4:3) variant is implemented but only lightly eyeballed.
- The font is an 8x8 ASCII subset row-doubled to 8x16, not full CP437.
