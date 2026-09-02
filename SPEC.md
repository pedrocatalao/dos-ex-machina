# DOS ex Machina — Specification

*v0.3 — 2026-08-30. Author: Pedro Catalão (with Claude). Status: pre-implementation.*

## 1. Vision

A standalone, always-fullscreen application for macOS, Linux and Windows that
puts a complete retro PC on your screen: a beige case and CRT monitor rendered
as a scene, the monitor's curved tube occupying a sub-region of the display.
The machine powers on, POSTs, and boots into a simulated MS-DOS prompt drawn
inside the tube. From that prompt the user lists, installs and launches
natively-ported DOS games — starting with SkyRoads
([pedrocatalao/skyroads-sdl](https://github.com/pedrocatalao/skyroads-sdl)).
Games run on the same simulated tube, inside the same bezel. Quitting a game
returns to `C:\>`. Quitting the machine is the only way back to the modern
desktop.

**The fiction never breaks.** No windows, no menus, no settings dialogs — every
interaction happens through the machine itself.

## 2. Product principles

1. **Fiction first.** The prompt is the UI. Installing a game is a DOS command.
   Errors are DOS-style messages. No modern UI chrome anywhere.
2. **Native ports, not emulation.** Games are C ports compiled into the app as
   cores — *dependency-free* C, linking no SDL and no windowing library of
   their own (§4.2). This is not DOSBox and must never try to be; the DOS
   shell is theater, not a compatibility layer.
3. **Data is downloaded, code is not.** All game cores ship compiled into the
   app binary. The in-prompt "install" fetches only each game's *data files*
   from official/legal sources (same model as skyroads-sdl's `get_data.sh`).
   Never download executable code at runtime (signing, security, Gatekeeper).
4. **Games stay standalone.** Each game repo keeps its normal standalone
   builds. DXM consumes games as an *additional* build target, never a fork.
5. **Authentic by default.** Default effects mimic a real ~1993 VGA CRT.
   Adjustments exist, but they are **physical knobs on the chassis** (§6.8),
   not hotkeys and not a dialog. You do not type a brightness — you turn it.
   Discrete, non-continuous options may use an in-fiction utility (a fake
   `MODE` or `CRT.EXE`) where a knob makes no sense.

## 3. Platforms & distribution

Same matrix as skyroads-sdl: macOS universal .app (Apple Silicon + Intel),
Linux x86_64/arm64 tarball (bundled **SDL3**, `$ORIGIN/lib` rpath), Windows
x86_64/arm64 zip (MinGW via MSYS2, bundled DLLs). Reuse the packaging and CI
*patterns* from skyroads-sdl verbatim (the SDL version differs — §12.1 — but
the bundling, rpath and release-attach mechanics are identical):

- `make_mac.sh`, `make_linux.sh`, per-platform GitHub workflows with badges
  (`.github/workflows/{macos,linux,windows}.yml` in skyroads-sdl are the
  reference — including the lessons baked into them: `fail-fast: false`,
  release-attach with runner bash not MSYS2 shell, no MSYS2 base update).
- Fully self-contained downloads. SDL3 bundled on Linux/Windows.
- **Minimum GPU requirement**, a consequence of the SDL_GPU choice (§12.1):
  Metal on macOS (any Mac that runs a current OS), D3D12 on Windows, **Vulkan
  1.0 on Linux**. There is deliberately no GL fallback — a second backend would
  mean a second shader path, which §6.7 forbids outright. Any machine from
  roughly 2014 onward qualifies; this is stated in the README, not discovered
  at runtime by a crash.

## 4. Architecture overview

```
+------------------------------------------------------------------+
| DXM shell (owns the ONE window, GPU device, audio device)        |
|                                                                  |
|  layout/       host resolution -> variant + module placement     |
|  chassis/      procedural case: modules drawn, no raster art     |
|  tube/         320x200x8 surface -> shader pipeline in bezel     |
|  fx/           persistence, burn-in, curvature, beam/mask,       |
|                bloom, glass, bezel spill  (one shader path)      |
|  dos/          boot theater + fake DOS prompt + command set      |
|  catalog/      game registry, data fetcher, install state        |
|  corehost/     loads/runs game cores (thread per running core)   |
+------------------------------------------------------------------+
        | core API (C, header-only contract)
+-------v-----------------+  +------------------------+
| skyroads core           |  | future game cores ...  |
| (game code from         |  |                        |
|  skyroads-sdl, built    |  |                        |
|  with SKY_CORE, linked  |  |                        |
|  statically into DXM)   |  |                        |
+-------------------------+  +------------------------+
```

### 4.1 The core model — threaded, platform-seam based

Critical constraint discovered in skyroads-sdl: **the game owns its control
flow.** `main_menu()`, `intro()` and `game()` are blocking loops that call
`plat_present()` / `plat_pump()` internally (see `src/main.c`, `src/menus.c`,
`src/game_play.c`). The game is NOT tick-based and must not be rewritten to be
(that would be major surgery on faithful code).

Therefore: **each running core gets its own thread.** The host implements the
game's platform interface; synchronization happens at the `plat_present()`
boundary:

- Game thread calls `plat_present()` → adapter copies the 320x200 8-bit
  framebuffer + 256×3 palette into a double buffer, signals the shell, and
  (to preserve pacing) sleeps until the shell has consumed a frame.
- Shell render loop picks up the latest published frame each display frame
  and runs it through the tube pipeline.
- Input flows the other way: shell collects SDL events, translates to the
  game's key mask / getch codes, adapter serves them from `plat_keys()` /
  `plat_getch()` on the game thread.
- `Time` (the 36.4 Hz tick, PIT divisor 0x19e4 — see skyroads-sdl
  `src/platform.c` `TICK_HZ`) stays derived from wall clock; the adapter
  reproduces `plat_tick_update()` semantics.

Only one core runs at a time (v1). Stopping a core: shell sets a quit flag the
adapter returns from `plat_pump()`; game exits its loops normally and the
thread joins. A core that won't exit within ~2s may be force-killed only at
app shutdown.

### 4.2 The core API (contract sketch)

> **[PORTING.md](PORTING.md) is now normative for this contract.** The sketch
> below is retained as orientation, but two things in it are superseded:
> `fb_w/fb_h` fixed at 320x200 (which contradicted §7's 720x400 text mode) is
> replaced by declared `dxm_mode`s carrying pixel aspect and physical scanline
> count; and the adapter moves from the game repo into DXM (§4.3). Where the
> two documents disagree, PORTING.md wins.

Header `dxm_core.h`, pure C, no SDL types exposed:

```c
typedef struct dxm_core_info {
    const char *id;           /* "skyroads" — also the DOS command name  */
    const char *exe_name;     /* "SKYROADS.EXE" — shown in DIR           */
    const char *title;        /* "SkyRoads"                              */
    const char *publisher;    /* "BlueMoon Software"                     */
    int         year;         /* 1993                                    */
    int         fb_w, fb_h;   /* 320, 200 (v1: must be 320x200)          */
    const char *data_probe;   /* file that proves data present:          */
                              /*   "roads.lzs" (case-insensitive)        */
} dxm_core_info;

typedef struct dxm_host {    /* provided BY the shell TO the core */
    /* framebuffer publish: core calls once per game frame */
    void (*present)(const uint8_t *fb, const uint8_t pal[256][3]);
    /* input: bitmask matches skyroads-sdl K_* enums; getch DOS-style */
    unsigned (*keys)(void);
    int      (*getch)(void);
    int      (*should_quit)(void);       /* shell wants the core gone   */
    double   (*now)(void);               /* monotonic seconds           */
    void     (*sleep_ms)(int ms);
    /* audio: core pushes interleaved s16 stereo; host mixes */
    void (*audio_push)(const int16_t *frames, int nframes, int rate);
    const char *pref_dir;                /* per-game save/cfg directory */
} dxm_host;

/* each core exports exactly these (statically linked, prefixed): */
const dxm_core_info *skyroads_core_info(void);
int  skyroads_core_main(const dxm_host *host, const char *data_dir);
     /* runs the whole game (blocking; called on the core thread);
        returns when the game exits */
```

Notes:
- v1 keeps it minimal: 320x200x8 only, s16 stereo audio, keyboard only.
- The adapter that maps skyroads-sdl's `platform.h` calls onto `dxm_host`
  lives in the skyroads repo (see §5) so the game code never changes.
- **The core must link no SDL at all.** This is a hard requirement, not a
  cleanup. Audited in skyroads-sdl: `SDL_` appears only in `src/platform.c`
  (59 uses, replaced wholesale by the adapter) and `src/audio.c` (18 uses —
  one mutex plus one `SDL_OpenAudioDevice` at line ~275). Everything else —
  `render.c`, `game_play.c`, `game_sim.c`, `menus.c`, `assets.c` — is already
  zero. Routing `audio.c` through `audio_push` and putting its mutex behind a
  two-function host hook therefore leaves the core SDL-free for the cost of an
  afternoon.
- Why that matters beyond tidiness — three reasons, the first of which is a
  hard blocker:
  1. **SDL2 and SDL3 both export `SDL_*` symbols.** DXM links SDL3 (§12.1)
     while skyroads-sdl's *standalone* build stays on SDL2 until it migrates
     (§10, M0b). If the core dragged SDL2 in, one binary would contain two
     incompatible SDLs claiming the same symbol names. An SDL-free core is
     what makes the two repos independently versioned rather than lockstep.
  2. It makes the shell's SDL version and graphics API DXM's decision alone,
     not a constraint inherited from every game repo it ever consumes. This is
     the property that lets game #2 arrive on whatever stack it likes.
  3. It removes the two-audio-devices problem (the original v1 plan) and lets
     the shell mix UI sounds, own master volume (§9) and apply the power-off
     "thunk" from day one.

### 4.3 Required changes in skyroads-sdl (Milestone 0)

Small, behavior-neutral, done in the game repo:

1. Extract the platform contract already implicit in `src/platform.h` — no
   signature changes, just ensure nothing in game code reaches around it
   (`render.c` is already 100% platform-free; `game_play.c` uses only
   `plat_*`/`Time`; `menus.c`/`assets.c` a handful of calls — audit the ~11
   uses).
2. Add CMake option `SKY_CORE`: builds game code (everything except `main.c`
   and `platform.c`) as a static library `skyroads_core`, plus a ~40-line
   `src/dxm_entry.c` (info struct + call into the game's top-level flow).
   **The adapter itself is NOT written here** — because `platform.h` is
   standardized across all ports, DXM ships one shared
   `corehost/platform_dxm.c` and every game reuses it. This is a change from
   v0.1 and it makes game #3 cheaper than game #2. See PORTING.md §1.
2c. Fix the embed-hostility violations found by the audit, all in game code:
   replace `exit()` with `plat_exit()` at `menus.c:20`, `menus.c:293`,
   `game_play.c:282`, `game_play.c:877`, `assets.c:43`; remove the cwd
   fallback at `compat.c:63`; route `assets.c:41`'s `fprintf(stderr,...)`
   through `host->log`; add `sky_reset_state()`; prefix exported globals
   (`Cur`, `Time`, `Cars_Seg`, ...). PORTING.md §3 has the rationale for each.
   **Without this, `KEY_ESC` in the SkyRoads menu terminates DXM itself.**
2b. Make `src/audio.c` SDL-free under `SKY_CORE`: replace
   `SDL_OpenAudioDevice`/`SDL_PauseAudioDevice` with pushes to
   `dxm_host.audio_push`, and the single `SDL_mutex` with a host-provided
   lock/unlock pair (or a C11 `mtx_t` where available). The standalone build
   keeps the SDL path unchanged behind the same seam. After this, verify with
   `nm` that `libskyroads_core.a` contains no undefined `SDL_*` symbols — that
   check belongs in CI, since it is the property everything in §6.5 rests on.
3. F10/CRT effects and fullscreen handling remain in `platform.c` — i.e. the
   standalone build keeps them; the core build excludes them (the shell owns
   all presentation). F9 (music mode) stays in the core: route as a normal
   key through `getch`/hook.
4. Tag a release once merged; DXM pins that tag.

## 5. Repo layout (this repo)

```
dos-ex-machina/
├── SPEC.md                  (this file)
├── CMakeLists.txt           FetchContent: skyroads-sdl @ pinned tag (SKY_CORE)
├── src/
│   ├── main.c               entry, SDL init, fullscreen appliance loop
│   ├── gpu.c/.h             thin GPU layer: context, targets, shader load,
│   │                        fullscreen-pass dispatch (the ONLY file that
│   │                        knows which graphics API we chose — §12.1)
│   ├── layout.c/.h          constraint solve: resolution -> variant
│   │                        (Compact/Standard/Stereo) -> slots -> metrics
│   ├── chassis.c/.h         procedural case modules: bevels, vents, grilles,
│   │                        LEDs, floppy, slider, lettering (§6.1-6.2).
│   │                        One draw fn per module; slots come from layout.
│   ├── chassis_params.h     every dimension/colour of the machine, one place
│   ├── tube.c/.h            fb -> texture; owns persistence/burn-in targets
│   ├── fx.c/.h              pass graph & ordering (§6.4), pass constants
│   ├── dos.c/.h             boot theater, prompt, command interpreter
│   ├── font.c/.h            CP437 8x16 VGA font (public-domain bitmap font)
│   ├── catalog.c/.h         game registry, install state, data verification
│   ├── fetch.c/.h           https downloads (see §8) + sha256 verify
│   ├── corehost.c/.h        thread mgmt, dxm_host implementation, mixing
│   ├── platform_dxm.c       THE shared adapter: standard platform.h ->
│   │                        dxm_host. Written once, reused by every port
│   │                        (PORTING.md §1) — not per-game code.
│   ├── knobs.c/.h           knob modules, hit-testing, detents, persistence
│   └── dxm_core.h           the core contract (§4.2)
├── shaders/                 one HLSL source per pass + the precompiled IR
│                            (SPIR-V / DXIL / MSL) produced ONCE by
│                            SDL_shadercross, baked into the binary as C byte
│                            arrays. Never compiled per-platform, never loaded
│                            from disk at runtime (§6.7).
├── tests/golden/            reference frames + tolerances for §6.7 CI
├── assets/                  sounds only (POST beep, key clicks, hum).
│                            NO scene art — the machine is drawn (§6.1).
├── make_mac.sh  make_linux.sh
└── .github/workflows/       macos.yml linux.yml windows.yml (copy patterns
                             from skyroads-sdl)
```

Local reference during development: skyroads-sdl checkout at
`/Users/pedro/Git/skyroads-mac` (repo: `pedrocatalao/skyroads-sdl`).

## 6. The scene & tube

### 6.1 Procedural, not painted

There is no raster scene art. The machine — case, bezel, vents, speaker
grilles, LEDs, floppy slot, volume slider, lettering — is drawn from
parameters at runtime, sized to whatever fullscreen resolution it finds.

`all-in-one-pc-dos.png` in this repo is a **look reference only**: proportions,
plastic and phosphor palette, control placement, the all-in-one silhouette. It
is never loaded and never shipped.

Why:

- **Resolution independence.** The reference is 1672x941; on a Retina or 4K
  panel a painting of that size is visibly soft, and there is no resolution at
  which one raster covers 1080p through 6K. Procedural geometry rasterises
  crisp at the native drawable size, always.
- **Aspect independence.** No aspect buckets, no 9-slice seams, and no
  non-uniform stretch of a photographed chassis on 16:10 or 21:9 (§6.3).
- **The chassis is live.** LEDs, slider position, power-button depression and
  screen-light spill are animated state rather than baked pixels (§6.6). A
  painted skin can only cross-fade between baked variants.
- **No art pipeline.** The machine is a header of constants (`chassis_params.h`)
  tunable in one place, which also closes the sourcing question that was
  §12.1 in v0.1.

The cost is paid once, not per frame: the static chassis renders into an
offscreen target at startup and on resolution change only. Per frame it is one
textured quad — the same cost as a raster skin — plus a handful of small quads
for the live elements.

### 6.2 Drawing primitives (no platform vector libraries)

Deliberately none of Cairo, Skia, CoreGraphics or Direct2D: three more build
problems, and three subtly different rasterisers, which is precisely what §6.7
forbids. Everything is built from:

- **`SDL_RenderGeometry` with per-vertex colours** for every gradient, bevel
  and shaded plastic face.
- **Startup-generated CPU surfaces** for rounded corners, vent slots, grille
  dot arrays and the plastic speckle/noise tile — plain C, integer math,
  byte-identical output on all three platforms, uploaded once.
- **The compiled-in CP437 8x16 bitmap font** for all chassis lettering, not
  just the DOS text. No system fonts anywhere: CoreText, fontconfig and GDI
  disagree on hinting, shaping and subpixel placement, and there is no
  configuration that makes them agree. A bitmap font is the only way the
  "PC-486"-equivalent plate and the port labels are identical everywhere.

### 6.3 Layout: modules in slots, reconfigured per aspect

Always fullscreen (`SDL_WINDOW_FULLSCREEN_DESKTOP`), any resolution, any
aspect. **The governing rule: the machine reconfigures, it never stretches.**
Extra width buys more machine, not a wider machine.

**The abstraction.** The chassis is not one drawing — it is a set of
*modules*, each a draw function with an intrinsic aspect and a minimum size:
brand plate, power button, LED cluster, floppy bay, speaker grille, volume
slider, phones jack, badge strip. The layout solver assigns modules to *slots*
(left column, right bay, bottom band, under-screen strip) according to the
host aspect. Because the case is procedural (§6.1), a module is a function
call — reconfiguring costs nothing, where a painted skin would need a whole
new painting per variant.

**The solve, tube-first:**

1. **Tube.** The largest 4:3 rectangle that fits the target height fraction —
   with the **6/5 VGA pixel-aspect correction** (320x200 displayed 4:3, *not*
   16:10; see skyroads-sdl `platform.c` line ~36, `VGA_H * scale * 6 / 5`).
   This is a fidelity hill worth dying on, and it is invariant across every
   variant below.
2. **Bezel** derived from the tube by fixed inset ratios. Also invariant.
3. **Variant selection** from the host aspect, which decides the slot set.
4. **Module assignment and packing** into those slots.

**Variants** (all of which existed as real 1993-ish multimedia PCs, which is
why this reads as authentic rather than as responsive-design):

| Host aspect | Variant | Configuration |
|---|---|---|
| ~1.25–1.45 (5:4, 4:3) | **Compact** | Narrow right bay: brand plate over power/LEDs, floppy below. Speakers in a slim bottom band. Everything tight to the tube. |
| ~1.45–1.85 (16:10, 16:9) | **Standard** | The reference machine: tube left, single right bay (brand, power, LEDs, floppy), full bottom band with twin grilles flanking volume + phones. |
| ~1.85–2.4 (21:9) | **Stereo** | **Speakers graduate to full-height columns flanking the CRT** — a tall grille on the left, its mirror on the right, with the drive/control bay outboard of the right speaker. Bottom band slims to a badge strip. |
| > 2.4 (32:9, dual-wide) | **Stereo + desk** | Stereo variant at its maximum sane width, remaining space filled by the procedural dark room/desk gradient. Past this point more machine looks absurd; a desk does not. |

Two rules keep this from feeling like four different products:

- **Shared metrics are literally shared.** Bezel inset, plastic palette, bevel
  radius, lettering size and LED geometry are the same constants in every
  variant. Only slot assignment changes.
- **Continuous within a variant, snapped between.** Inside a band the bays and
  grilles grow smoothly with available width; the variant switch happens at a
  threshold. Since the app is fullscreen and the resolution rarely changes
  mid-session, nobody watches a variant transition — but the layout is
  recomputed on display change, so it must be correct, not just stable.

All metrics are expressed in units of tube height, so 1080p and 5K produce the
same picture at different sampling densities.

**Consequences elsewhere:** the golden-frame CI (§6.7) tests one resolution
*per variant*, not one resolution overall — a layout regression on 21:9 is
exactly the kind of thing that otherwise ships unnoticed. And the bezel-spill
geometry (§6.6) must be derived from the solved layout rather than hardcoded,
since in the Stereo variant the tube is flanked by grille rather than by flat
plastic, and grille should catch the glow differently.

**HiDPI, per platform.** Size everything from `SDL_GetRendererOutputSize` (or
the GL drawable size), never `SDL_GetWindowSize` — they differ by the backing
scale on macOS and under fractional scaling on Wayland, and conflating them is
the classic source of a half-size or quarter-resolution scene. Request
`SDL_WINDOW_ALLOW_HIGHDPI`. On Windows, declare per-monitor DPI awareness in
the manifest or the OS bitmap-stretches the whole window and every effect in
§6.4 is destroyed before it reaches the glass.

### 6.4 The tube pipeline

Explicit pass order. **Everything below runs in linear light; sRGB encode
happens once, in the final pass.**

| # | Pass | Notes |
|---|------|-------|
| 0 | Framebuffer upload | indexed -> RGB on CPU (as skyroads `plat_present`), 320x200 game or 720x400 text, integer-sampled |
| 1 | **Phosphor persistence** | ping-pong accumulator, per-channel exponential decay (green persists longer than blue on real P22) |
| 2 | **Burn-in** | second accumulator, time constant in *minutes*, added back at low weight |
| 3 | **Curvature** | per-pixel inverse barrel map, evaluated at output resolution |
| 4 | **Beam, mask & bleed** | scanline beam profile integrated over the pixel footprint; shadow-mask/aperture-grille triad modulation; small horizontal kernel for CRT pixel light bleed |
| 5 | **Bloom / glow** | downsample-blur chain at a *fixed internal resolution*, added back |
| 6 | **Glass** | static specular sheen + vignette |
| 7 | **Bezel spill** | heavily downsampled tube lights the surrounding plastic (§6.6) |
| 8 | **Encode** | linear -> sRGB |

Three ordering rules that are easy to get wrong:

- **Mask and scanlines come *after* curvature, in output space.** Warping the
  mask along with the content moirés badly, and the phosphor grid belongs to
  the physical glass — it must stay pinned to output pixels while the image
  bends behind it.
- **Bloom is fed from the pre-mask image** (step 3's output), so glow radiates
  from beam energy rather than from the mask's dark gaps. Feeding it post-mask
  produces a characteristic dirty haze.
- **Scanline modulation must happen in linear light.** Doing it in gamma space
  is the classic CRT-shader mistake and reads far too dark, which people then
  "fix" by weakening the effect until it looks like a grey overlay.

The beam profile is integrated over each output pixel's footprint rather than
point-sampled. Without that, scanlines alias into a shimmering moiré whenever
the tube height is not a clean multiple of 200 — which is to say, on almost
every real display. This is the single biggest determinant of whether the
effect looks like a CRT or like a filter.

### 6.5 Why this requires shaders (revising v0.1)

v0.1 specified pure `SDL_Renderer`, no custom shaders, curvature via a 32x24
warped mesh. That does not survive the effect list, for four reasons:

1. A mesh warp is piecewise-linear — visible faceting toward the corners, and
   no per-pixel control over how the source is sampled.
2. Scanlines, mask and bleed are per-pixel functions of subpixel position.
   `SDL_Renderer` has no way to express them.
3. Persistence and burn-in accumulators need ping-pong targets with per-channel
   decay math that `SDL_Renderer`'s fixed blend modes cannot do.
4. **Most importantly for the cross-OS requirement:** `SDL_Renderer` dispatches
   to Metal on macOS, D3D11 on Windows and GL/GLES on Linux. That is three
   different rasterisers with three different blend-rounding and filtering
   implementations. "Pure SDL_Renderer" is the *least* identical-looking option
   available, not the most.

So: **one hand-written shader pipeline, one source, one code path, all three
platforms.** That pipeline runs on **SDL3 + SDL_GPU** (§12.1), and `gpu.c` is
the only file that names an SDL_GPU type.

### 6.6 Light bleed onto the plastic

The glow spilling from the tube onto the bezel is where the procedural chassis
earns its keep. Because the case is drawn from parameters, per-pixel
*distance-to-screen* and *surface normal* are known quantities: the spill can
respect the geometry, so the inner bezel face catches strong light, the outer
face catches a grazing amount, and the bottom band barely any. Bright frames
visibly wash the plastic; the DOS prompt on black barely lifts it.

Source is the tube downsampled to roughly 16x12, so the spill carries the
*colour* of what is on screen — SkyRoads' orange terrain warms the bezel — at
negligible cost. A painted skin can only cross-fade a single baked glow.

### 6.7 Faithfully equal on macOS, Linux and Windows

State the achievable target honestly: **bit-identical GPU output is not
attainable**, and the variation between GPU vendors (Apple, AMD, NVIDIA, Intel)
is larger than the variation between operating systems. The target is *no
perceptible difference*, reached by eliminating every source of variation we
actually control:

- **One shader source, one graphics API** (§12.1). No per-platform variants,
  no `#ifdef`-ed effect code, no backend fallback.
- **The shaders are compiled once, not three times.** This is the strongest
  single argument for the SDL_GPU choice and it is easy to miss. Under
  OpenGL, each vendor's GLSL front-end compiles our source independently —
  three parsers, three sets of built-in implementations, three precision
  defaults, three optimiser philosophies, all applied to *high-level* code.
  Under SDL_GPU we run SDL_shadercross **offline, on one machine**, and ship
  precompiled SPIR-V / DXIL / MSL. The drivers still lower IR to machine code,
  but every semantic decision above that line was made once by one compiler.
  The divergence surface shrinks from "three front-ends" to "three
  back-ends".
- **No driver-dependent filtering.** Every tap uses `texelFetch` with
  hand-written weights. Never rely on the driver's bilinear, its mipmap
  generation, or edge wrap behaviour — all three differ measurably.
- **Explicit precision.** `highp` declared everywhere; never let a GLES-class
  default silently demote a pass to `mediump`.
- **Time-based decay, never frame-based.** skyroads-sdl currently decays
  phosphor by `FX_PERSIST 214 / 256` **per frame** (`platform.c:18`), so on a
  120 Hz ProMotion Mac the trail is half the length it has on a 60 Hz Linux
  box. In DXM every accumulator constant is a **half-life in seconds**, applied
  as `pow(k, dt)`. Same for burn-in, warm-up and the power-off collapse.
- **Fixed internal resolutions** for the persistence, burn-in and bloom
  buffers. If these track output resolution, glow radius and trail length
  change from machine to machine — the exact opposite of the goal.
- **Explicit colour handling.** Linear throughout, sRGB encode last, no
  reliance on the default framebuffer's format or on driver sRGB conversion.
- **Colour management is the largest genuine risk.** macOS composites through
  the display's ICC profile; Windows and Linux typically pass through
  untouched. The identical phosphor green will therefore *measure* differently
  on the Mac unless it is handled deliberately (§12.6).
- **Golden-frame CI.** Each platform workflow renders a fixed set of frames
  headless, **one resolution per layout variant** (§6.3), with a fixed clock
  and **every knob at its factory detent** (§6.8),
  and compares them against references in `tests/golden/` within a tight
  perceptual tolerance. Linux CI needs a software Vulkan driver (lavapipe) for
  headless capture; macOS and Windows capture on their native backends, and the
  tolerance is what absorbs the remaining back-end delta.
  This is what turns "faithfully equal" into a claim that fails the build
  rather than an aspiration.

**Power theater:** power-on = click sound, degauss wobble, phosphor warm-up
(~1.5s); POST text; boot. Power-off (`EXIT`) = the power button visibly
depresses, then collapse-to-horizontal-line + thunk, with the burn-in buffer
lingering a beat on the dying phosphor. App start-to-prompt under 4 seconds; a
keypress skips theater.

**Performance:** target 60fps at 4K on integrated GPUs. The only
full-resolution passes are curvature+mask and the final composite; bloom and
bezel spill run at reduced fixed resolutions, and the chassis is a cached
texture. Degradation, if ever needed, drops bloom quality first and never the
beam profile.

### 6.8 The knobs — configuration without a settings screen

Physical controls on the case, turned by hand. This is how DXM has
configuration *at all* without violating §2.1: there is no preferences dialog
because you reach out and turn CONTRAST instead.

Knobs are modules in the §6.3 sense — they occupy slots, and the layout
variant decides placement. That maps onto how the machines being imitated
actually worked: everyday controls on the front, set-and-forget ones behind a
flip-down service door. The Compact variant puts more behind the door; the
Stereo variant has room to expose them.

| Control | Placement | Drives |
|---|---|---|
| VOLUME | front (already in the reference machine) | §9 master mix |
| BRIGHTNESS | front | tube black level + beam gain |
| CONTRAST | front | tube gain curve |
| AMBIENT | service door | room light — see below |
| GLOW | service door | §6.4 bloom **and** §6.6 bezel spill, together |
| DEGAUSS | service door, momentary | fires the degauss wobble |
| V-HOLD | service door | rolls the picture. Purely an easter egg, and irresistible |

**AMBIENT is not one effect, it is four coupled ones.** Raising room light
must simultaneously: (1) brighten the surround gradient (§6.3), (2) add
diffuse light to the plastic — real lighting of known geometry, which the
procedural chassis makes possible and a painted skin does not (§6.6),
(3) strengthen the glass reflection over the tube (§6.4 step 6), and (4) **lift
the tube's apparent black level**, because a CRT in a lit room has grey blacks,
not black ones.

Wiring AMBIENT to a vignette alone would read as a dimmer applied to a
photograph. Wiring all four makes the machine look like it is sitting in a
room. The same coupling logic applies to GLOW: a brighter tube lights its own
bezel more, so one knob must move both the bloom and the spill or the
relationship between them breaks.

**Interaction without breaking the fiction.** The system cursor stays hidden
(§2.1). Knobs highlight on hover and are turned by dragging, with the knob
itself as the only feedback — no tooltip, no value readout, no overlay. Every
knob is also keyboard-reachable, so the appliance never *requires* a mouse.

**Consequence for §6.7, and it is easy to miss:** knob positions are per-user
state that feeds the render. Golden-frame capture must therefore reset every
knob to a **factory detent** first, or the cross-platform comparison is
silently measuring someone's contrast setting instead of the pipeline.
Positions persist in the pref dir, alongside the §12.3c burn-in decision.

## 7. The fake DOS

- **Prompt:** `C:\>` and `C:\GAMES>`, CP437 8x16 font, blinking block cursor,
  80x25 text grid rendered into the same 640x400-ish text framebuffer
  composited onto the tube (text mode may use its own higher-res buffer than
  game mode — real VGA did too).
- **Commands (v1):** `DIR` (lists installed games as .EXE entries with fake
  sizes/dates + not-installed ones marked or via `CATALOG`), `CD`, `CLS`,
  `HELP`, `VER` (easter egg: "DXM-DOS 1.0 [c] 2026"), `TYPE README.TXT`,
  `<GAME>` / `<GAME>.EXE` to launch, `INSTALL <GAME>`, `UNINSTALL <GAME>`,
  `EXIT` (power off). Unknown input → `Bad command or file name`.
- **Install flow:** `INSTALL SKYROADS` → period-styled progress
  (`Downloading SKYROADS.ZIP from bluemoon.ee ... 412K [######--] 78%`) →
  unpack → verify probe file → `1 file(s) copied. Type SKYROADS to play.`
  Failures are DOS-flavored but honest (real error + retry hint).
- **Boot script** lives as data (`assets/boot.txt`-ish) so tweaking theater
  doesn't touch code. Include the classic memory count, a fake BIOS line
  crediting the real machine ("DXM BIOS v1.0 — 640K OK"), and
  `Starting MS-DOS...`.
- Easter eggs encouraged (`FORMAT C:` → refuses amusingly) but ship v1 lean.

## 8. Catalog & data fetching

- `catalog.c` holds a compiled-in registry (v1: array of structs; no runtime
  manifest): id, title, data source URLs, archive type, expected sha256s,
  probe file, unpack rules. SkyRoads entry mirrors `get_data.sh`:
  `https://www.bluemoon.ee/history/skyroads/skyroads.zip` (+ TimGM6mb
  soundfont from SourceForge for wavetable mode).
- Downloads via libcurl (Linux: link system libcurl; mac: system
  libcurl.4.dylib is a stable install; Windows: winhttp or bundled
  libcurl — decide in impl; NO custom TLS).
- Data lives under the platform pref dir
  (`SDL_GetPrefPath("DOSexMachina", "dxm")/games/<id>/`), one dir per game,
  passed to the core as `data_dir`. Verify sha256 after download; probe file
  check on every launch (missing → prompt suggests `INSTALL`).
- Per-game save/cfg dir (`.../games/<id>/save/`) passed as `pref_dir` so the
  skyroads core's `skyroads.cfg` lands there, not in the standalone app's
  location (no cross-contamination between DXM and standalone installs).

## 9. Audio

Shell owns the single audio device (48kHz s16 stereo) and mixes: core audio
via `audio_push` (v1 — the core opens no device of its own, §4.2), UI/theater
sounds (POST beep = square wave, key clicks off by default, optional CRT hum
at -40dB for sickos). Master volume via fiction (`VOLUME 0-9` command).

## 10. Milestones

- **M0 (skyroads-sdl repo):** platform-seam audit + `SKY_CORE` static lib +
  `core_adapter.c` + SDL-free audio path + tag. CI asserts no undefined `SDL_*`
  in the core archive. Standalone builds provably unchanged (CI green,
  byte-compare unaffected artifacts if feasible).
- **M0b (skyroads-sdl repo, PARALLEL — does not block anything):** migrate the
  *standalone* build from SDL2 to SDL3. Scope is genuinely small: the audit in
  §4.2 found `SDL_` in exactly two files, and M0 has already rewritten most of
  what is in them. Mostly mechanical renames (`SDL_RenderCopy` →
  `SDL_RenderTexture`, `SDL_KEYDOWN` → `SDL_EVENT_KEY_DOWN`, keysym access,
  `SDL_OpenAudioDevice` → `SDL_OpenAudioDeviceStream`). Sequencing note: **this
  is a parallel track, not a prerequisite for any DXM milestone.** The
  SDL-free core (M0) is what buys that independence, so if schedule pressure
  arrives, M0b is the thing that slips — never M0.
- **M1 (this repo):** fullscreen appliance + procedural chassis + constraint
  layout across aspects + the full tube pipeline (§6.4) on a static test
  framebuffer + boot theater + prompt with DIR/CLS/HELP/EXIT. Golden-frame
  harness stood up here, not retrofitted. *Demoable GIF.*
- **M2:** skyroads core linked via FetchContent; `SKYROADS` launches it in
  the tube; Esc-quit returns to prompt. *The money demo.*
- **M3:** INSTALL flow with real download + verify; first-run experience.
- **M4:** packaging (3 platforms, self-contained) + CI + golden-frame
  comparison green on all three + signed/notarized-ish mac app (ad-hoc like
  skyroads).
- **M5:** polish (power on/off theater, burn-in and bezel-spill tuning, glass
  and vignette pass, sounds),
  then public repo + landing page + announce with M2 GIF.

## 11. Non-goals (v1)

- Real DOS emulation or running arbitrary DOS executables (that's DOSBox).
- Downloading/loading code at runtime; plugin systems; mod APIs.
- Windowed mode (except a hidden `--windowed` dev flag), settings *UI*,
  gamepad, save states, netplay, more than one simultaneous core.
  *(Settings themselves are not a non-goal — knobs on the chassis (§6.8) are
  the configuration surface. What stays banned is a dialog, an overlay or a
  value readout, i.e. anything that admits the machine is software.)*
- *(v0.1 listed "shaders/GL backends" here. Removed: §6.5 makes a single
  hand-written shader pipeline the requirement rather than a stretch goal.
  What remains a non-goal is user-selectable backends — there is exactly one.)*
- Bundling any game data in the repo or releases (same legal stance as
  skyroads-sdl LICENSE: fetch from official sources only).

## 12. Open questions

1. ~~**Graphics API**~~ — **DECIDED (v0.3): SDL3 + SDL_GPU**, with shaders
   authored in HLSL and cross-compiled offline by `SDL_shadercross` to
   SPIR-V / DXIL / MSL. Backends: Metal (macOS), D3D12 (Windows), Vulkan
   (Linux). No GL fallback, no user-selectable backend.

   *Why this reverses the v0.2 recommendation.* v0.2 proposed SDL2 + OpenGL 3.3
   as the pragmatic choice, reasoning that alignment with skyroads-sdl's SDL2
   was worth GL's deprecation risk on macOS. Both halves of that turned out to
   be wrong. The alignment was illusory — an SDL-free core (§4.2) means
   skyroads never constrained DXM's stack in the first place — and the
   deprecation risk was the *weaker* of the two arguments against GL. The
   stronger one is fidelity: GL compiles our shader source three separate
   times with three vendor front-ends, while SDL_GPU compiles it once, offline,
   and ships IR (§6.7). For a project whose central requirement is *identical
   output on three operating systems*, that is close to decisive on its own.

   *What SDL3 additionally fixes,* each mapping onto a hazard this spec already
   documents:
   - **Pixel density is explicit** (`SDL_GetWindowPixelDensity`, sizes in
     pixels vs points properly separated) — the §6.3 HiDPI trap becomes hard
     to fall into rather than easy.
   - **Colourspace is requestable on the window**, which turns §12.3b from an
     unanswerable question into a one-line decision.
   - **`SDL_AudioStream`** with automatic resampling and logical device
     binding is a much better substrate for the §9 mixer than SDL2's callback.
   - **Fractional-scale Wayland** support is materially better.

   *Residual risks, accepted with eyes open:*
   - SDL_GPU is younger than the GL path. Mitigation: every SDL_GPU type stays
     inside `gpu.c`, so the pass graph in `fx.c` is portable if we ever need to
     move.
   - Vulkan-only on Linux, with no GL fallback (§3). Deliberate: a fallback
     backend means a second shader path, which is exactly what §6.7 forbids.
   - `shadercross` is a build-time dependency — but only on the one machine
     that regenerates IR, never in the three platform CI jobs.

2. Name/branding of the fake OS in the fiction ("DXM-DOS"? version string?)
   — must NOT claim to be Microsoft MS-DOS in any user-visible string
   (trademark); "Starting DXM-DOS..." is the safe play the fiction survives.
3. Windows fetch stack (winhttp vs bundled libcurl) and mac notarization
   (stay ad-hoc like skyroads, or invest in a Developer ID?).
3b. **Colour management on macOS** (§6.7): request a specific colourspace for
   the window so the display profile is bypassed and all three platforms feed
   the panel the same numbers, or accept the profile and let the Mac look
   "correct for that display" instead of "identical to Linux"? These are
   genuinely different definitions of faithful and the choice should be
   deliberate.
3c. **Burn-in persistence:** does the ghost reset each launch, or accumulate
   across sessions in the pref dir so a long-lived install slowly acquires a
   permanent `C:\>` shadow? The latter is a lovely detail and costs one small
   file.
4. Whether F9 wavetable toggling stays a game-level feature inside DXM or
   graduates to a `SETUP.EXE`-style fiction utility.
5. Game #2 candidate list and source-acquisition strategy (released-source
   games vs author outreach leveraging skyroads-sdl's track record).
