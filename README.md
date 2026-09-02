<p align="center">
  <img src="docs/logo.png" alt="DOS ex Machina" width="720">
</p>

A 1993 beige-box PC on your screen — case, CRT and all — booting a simulated
DOS prompt, from which you launch **natively ported** DOS games. The games run
in the same tube, behind the same glass, with the same phosphor.

> **Early development.** It runs end to end on macOS — boot, prompt, game,
> back to the prompt — but Linux and Windows are not there yet, and there is
> no way to install a game beyond pointing it at files you already have.
> Things will move around.

Nothing here is a photograph. The machine is drawn procedurally at your
display's resolution, from signed-distance geometry and a lighting model: the
moulding partings, the speaker pods, the vent cuts, the ejector-pin marks and
thirty years of wear are all solved, not painted. The tube is a real pipeline
— phosphor persistence, barrel curvature, footprint-integrated scanlines, an
aperture-grille mask, bloom, and coloured light spilling from the picture onto
the plastic around it.

<p align="center">
  <img src="docs/screenshot-prompt.jpg" alt="The DOS prompt" width="49%">
  <img src="docs/screenshot-game.jpg" alt="SkyRoads running in the tube" width="49%">
</p>

The games are not emulated. Each is a native C port that also ships as a
standalone game in its own right — [SkyRoads][sr] runs perfectly well on its
own — and DXM is the optional machine you can put it inside.
[PORTING.md](PORTING.md) is the contract a port satisfies to run here.

[sr]: https://github.com/pedrocatalao/skyroads-sdl

## How it works

A game reaches DXM one of two ways, and everything above the seam is identical
either way:

- **Linked in** at build time — what the dev build does.
- **As a `.dxm` module**, opened at run time with `dlopen`/`LoadLibrary`.

A module exports exactly three symbols (`dxm_core_get_info`, `dxm_core_main`,
`dxm_core_audio`) and hides everything else, so two games can be loaded at once
without their globals colliding. It carries the shared adapter inside it and
links no SDL and no threading library of its own, which is what makes it
loadable on every platform. `dxm_core_info.abi` is checked before anything
runs, so a module built against a different version of the contract is refused
with a message saying which side needs updating.

```
$ ./build/dxm --core ~/Downloads/skyroads-macos-universal.dxm
[dxm] core: SkyRoads (BlueMoon Software, 1993) from …/skyroads-macos-universal.dxm
```

## Build

Needs SDL3 and a [skyroads-sdl][sr] checkout for the bundled core.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDXM_SKYROADS=/path/to/skyroads-sdl
cmake --build build -j8
```

The subproject's options are set from here, so a plain configure works — you
should not have to remember `SKY_CORE`.

## Run

```bash
DXM_DATA=/path/to/skyroads/data ./build/dxm
```

Fullscreen is real fullscreen: the case fills the display edge to edge with no
background around it. Extra width becomes more machine — wider bays, wider
speaker columns on ultrawide — never letterboxing.

At the `C:\>` prompt:

| | |
|---|---|
| `DIR` | list the files on this machine |
| `NC` | dual-pane navigator, Norton Commander's shape |
| `SKYROADS` | run the game |
| `TYPE`, `CLS`, `VER`, `HELP` | as you would expect |
| `EXIT` | switch the machine off |

**F1** opens a settings panel over the tube with sixteen CRT parameters —
bloom, burn-in, static, jitter, glow line, ambient light, flicker, h-sync,
RGB shift, chassis glow, persistence, scanlines, pixel grid, curvature,
brightness, contrast. They save to `crt.cfg` in the preferences directory.

Esc out of a game returns to the prompt; the machine survives it and the game
can be relaunched.

Dev flags: `--windowed`, `--size WxH`, `--core FILE.dxm`, `--type CMD`,
`--shot out.bmp --frames N`, `--selftest`, `--ambient N`, `--dump-audio FILE`.

## Status

Working end to end: boot theater → `C:\>` → `SKYROADS` → the game runs in the
tube → Esc → the prompt again → it relaunches. `--selftest` runs that whole
sequence twice and is what proves PORTING §3.1 and §3.2 hold.

**Platforms.** DXM itself runs on macOS today. The *core module* builds and is
verified on macOS, Linux and Windows on both x86_64 and arm64 by the game
repo's CI — it is the shell that is behind, not the contract.

## Known gaps

- **Linux and Windows.** `gpu.c` reaches for `<OpenGL/gl3.h>` directly and
  needs a GL loader elsewhere; `corehost.c` still uses pthreads, `<unistd.h>`
  and `clock_gettime`, all of which have direct SDL3 equivalents.
- **No downloader.** `--core` takes a module you already fetched, and the game
  data directory comes from `DXM_DATA`. The catalogue — a manifest of games
  with module URLs, hashes and art — is designed but not written.
- **NC's right pane** shows a placeholder image rather than real game art.
- The chassis knobs are drawn but not yet interactive; the F1 panel is the
  working control surface.
- The font is an 8x8 ASCII subset row-doubled to 8x16, plus the CP437 line
  drawing the navigator needs — not full CP437.

## Design notes

[SPEC.md](SPEC.md) covers the machine: how the layout is solved from the host
resolution, the tube pipeline pass by pass, and why every decision that could
differ between platforms is pinned down instead.

[PORTING.md](PORTING.md) is the normative contract for a port: no `exit()`, no
stdio, no SDL, no working-directory assumptions, restartable state, declared
video modes, and audio pulled rather than pushed.
