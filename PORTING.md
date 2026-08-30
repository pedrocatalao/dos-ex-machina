# Porting MS-DOS games to run natively — and inside DOS ex Machina

*v1.0 — 2026-08-30. Companion to [SPEC.md](SPEC.md). Normative: a port that
violates a MUST here will not embed.*

## 0. What this document is for

Every game ported under this project ships **twice, from one source tree**:

1. as a **standalone native executable** (its own window, its own SDL, its own
   `main`) — the way skyroads-sdl ships today; and
2. as a **DXM core** — a dependency-free static library that DOS ex Machina
   links in and runs inside the simulated tube.

Neither target is a fork of the other (SPEC §2.4). This document is the
contract that makes both possible from the same code, and it exists *before*
the second port rather than after, because retrofitting it onto skyroads-sdl
turned up five separate `exit(0)` calls in game code that would each have
killed the host process (§3.1).

**Read §3 first if you are porting.** It is the part that is not obvious and
the part that bites.

## 1. Shape of a conforming port

```
yourgame/
├── src/
│   ├── <game logic>.c/.h    dependency-free C. No SDL, no windowing, no
│   │                        audio API, no direct file I/O (§3.6).
│   ├── platform.h           THE SEAM. Identical across all ports (§2).
│   ├── platform_sdl.c       standalone implementation. Owns window,
│   │                        renderer, audio device, fullscreen, hotkeys.
│   ├── main.c               standalone entry only.
│   └── dxm_entry.c          ~40 lines: the info struct + a call into the
│                            game's top-level flow (§4).
└── CMakeLists.txt           builds BOTH targets; `-DGAME_CORE=ON` selects
                             the core (no main.c, no platform_sdl.c).
```

The seam is `platform.h`, and it is **the same header in every port**. That is
the point: because the seam is standardized, the adapter that maps it onto
`dxm_host` is written **once, by DXM**, and shipped as
`corehost/platform_dxm.c`. A porter never writes an adapter — only
`dxm_entry.c`, which declares what the game is and calls its own entry
function.

This is a change from SPEC v0.3 §4.3, which put `core_adapter.c` in the game
repo. Moving it to DXM makes game #3 cheaper than game #2 and keeps
DXM-specific code out of the game repos entirely, which serves SPEC §2.4
better than the original plan did.

## 2. The contract

### 2.1 Video: modes are declared, never assumed

SPEC v0.3 hardcoded `320x200x8` into `dxm_core_info`, while SPEC §7
simultaneously required a 720x400 text mode. That contradiction is resolved
here: **a core declares each mode it can enter, and publishes the mode with
every frame.** A DOS game that shows a text-mode intro and then switches to
Mode 13h is normal, and must work.

```c
typedef enum { DXM_FB_INDEX8 = 0, DXM_FB_RGB888 = 1 } dxm_fb_format;

typedef struct dxm_mode {
    int  w, h;                /* framebuffer dimensions                      */
    dxm_fb_format format;
    int  par_num, par_den;    /* PIXEL aspect as WIDTH:HEIGHT.  320x200 is
                                 5:6 - pixels are TALLER than wide, which is
                                 what makes 320x200 display as 4:3 and not
                                 16:10.  (v1.0 said "6:5", which is the
                                 vertical STRETCH factor, not the pixel
                                 aspect - the reference implementation caught
                                 the ambiguity.)                             */
    int  crt_lines;           /* PHYSICAL scanlines this mode drove (§2.2)   */
} dxm_mode;

typedef struct dxm_frame {
    const dxm_mode *mode;
    const uint8_t  *pixels;      /* w*h bytes (INDEX8) or w*h*3 (RGB888)     */
    const uint8_t (*palette)[3]; /* 256 entries; NULL unless INDEX8          */
} dxm_frame;
```

Canonical modes:

| Mode | w x h | format | PAR | `crt_lines` |
|---|---|---|---|---|
| VGA 13h | 320x200 | INDEX8 | **5:6** | **400** |
| VGA Mode X | 320x240 | INDEX8 | 1:1 | 480 |
| EGA | 320x200 | INDEX8 | **5:6** | 400 |
| VGA text 80x25 | 720x400 | INDEX8 | 1:1 | 400 |
| VGA 12h | 640x480 | INDEX8 | 1:1 | 480 |

### 2.2 Why `crt_lines` is a separate field

Because it is the field that keeps the CRT effects honest, and getting it
wrong is the single most common mistake in retro rendering.

**Mode 13h is line-doubled.** A real VGA card drove 400 physical scanlines for
a 320x200 image — each logical row was scanned twice. So a faithful CRT shows
**400 scanlines, not 200**. Shader stacks that derive the scanline count from
framebuffer height draw 200 fat dark gaps and produce the over-striped look
that reads as "filter" rather than "monitor."

Deriving the beam and mask from `crt_lines` instead of from `h` means one
implementation is correct for 13h, Mode X, text mode and 640x480 alike — and
game #2 needs no FX retuning. This is what SPEC §6.4's constants must be
functions of.

### 2.3 Input: XT scancodes

The seam speaks **XT scancodes** — what a DOS game's INT 9 handler actually
saw. It is the faithful choice and it is self-documenting for a porter reading
original disassembly.

```c
int (*key_down)(int xt_scancode);  /* current physical state, for polling    */
int (*getch)(void);                /* BIOS-style buffered; 0 if none.        */
                                   /* extended keys arrive as 0x100|scancode */
```

A port keeps its own `K_*` enums if it has them; it maps them to scancodes in
one table.

### 2.4 Timing

The host provides a monotonic clock only. **Each game derives its own tick**,
because DOS games programmed the PIT differently:

```c
#define DXM_PIT_HZ 1193182.0        /* 8253 input clock                      */
double (*now)(void);                /* monotonic seconds                     */
/* skyroads: divisor 0x19e4, /5  ->  TICK_HZ = DXM_PIT_HZ / 0x19e4 / 5.0     */
```

**Never derive timing from frame count.** SPEC §6.7 documents why: it makes
behaviour differ between a 60 Hz and a 120 Hz display. This applies to game
logic exactly as it applies to effects.

### 2.5 Audio — pull, not push

**Revised in v1.1 by the reference implementation.** v1.0 specified a push
(`audio_push`), mirroring SPEC §9. Implementing skyroads-sdl showed pull is
strictly better: the game's mixer is *already* a pull callback
(`audio.c: audio_cb(void*, uint8_t*, int)`), so a push would mean the core
rendering into an intermediate ring buffer that the host then drains — one
extra buffer and its latency, bought for nothing.

So the core exports a render function and never opens a device:

```c
void <game>_audio_render(int16_t *out, int nframes);   /* s16 stereo, 48k */
```

The host owns the one device and calls this from its own callback (SPEC §9).
For skyroads this reduced the entire audio port to: swap the SDL mutex for a
pthread one, `#if SKY_CORE` out the `SDL_OpenAudioDevice` call, and export the
existing callback under a new name.

### 2.6 Files and saves

All data access goes through the host. The core never calls `fopen`.

```c
const char *data_dir;   /* read-only game data, host-installed              */
const char *pref_dir;   /* per-game saves and config, writable             */
```

## 3. The eight rules

These are the embed-hostility rules. Each one below was violated by
skyroads-sdl before this document existed, and each violation is cited — they
are field-observed, not hypothetical.

### 3.1 MUST NOT call `exit()`, `abort()`, or `atexit()`

**The rule that matters most.** In a standalone binary `exit(0)` is a
reasonable way to quit. In DXM it terminates the entire machine from inside a
guest — the user presses Esc in a game menu and the whole simulated PC powers
off, with no shutdown theater and no chance to save.

> Observed in skyroads-sdl: `menus.c:293` (`case KEY_ESC: exit(0);`),
> `menus.c:20`, `game_play.c:282`, `game_play.c:877`
> (`if (!plat_pump()) exit(0);`), and `assets.c:43` (`exit(1)` on OOM).

**Instead:** call `plat_exit(int code)`. In the standalone build it is
literally `exit()`. In the core build the adapter `longjmp`s back to the core
entry point, unwinding the game's blocking loops without touching their
structure.

`setjmp`/`longjmp` is the right tool here specifically because it demands *no*
restructuring of game control flow. The alternative — threading return codes
up through every call site — is the kind of surgery on faithful code that
SPEC §4.1 exists to avoid. The cost is that resources are not released on the
jump, which is why §3.2 exists.

### 3.2 MUST be restartable in the same process

The user types `SKYROADS`, plays, quits to `C:\>`, and types `SKYROADS` again.
That must work, and it must work after a `plat_exit()` longjmp left the
previous run's state arbitrary.

Every port exports `void <game>_reset_state(void)` which returns all
file-scope state to its initial value. Run-quit-run is a conformance test
(§5), not a promise, because this is the rule most likely to rot silently.

### 3.3 MUST NOT write to stdio or install signal handlers

`stderr` goes to a terminal the user cannot see, and a handler installed by a
guest outlives it.

> Observed: `assets.c:41` `fprintf(stderr, ...)`.

Diagnostics go through `host->log()`, which surfaces them in-fiction.

### 3.4 MUST NOT depend on the working directory

DXM's cwd is wherever the user's launcher happened to start it.

> Observed: `compat.c:63` — `if (!f) f = fopen(name, mode); /* fall back to
> cwd (cfg files) */`.

All paths resolve under `data_dir` or `pref_dir` (§2.6).

### 3.5 MUST NOT link SDL or any windowing/audio library

Non-negotiable, and not merely stylistic: DXM links SDL3 while a standalone
port may still be on SDL2, and **both export `SDL_*` symbols**. A core that
drags SDL in puts two incompatible SDLs into one binary. CI asserts this with
`nm` (§5).

### 3.6 MUST NOT create threads or block indefinitely

The host runs the core on one thread it owns (SPEC §4.1). A core must return
from `plat_pump()` promptly and must unwind within ~2 s of `should_quit()`.

### 3.7 MUST NOT read the environment

`getenv` for debug switches is fine standalone, forbidden in a core — the
guest must not be configurable from outside the fiction (SPEC §2.1).

### 3.8 MUST prefix its exported symbols

Statically linking N games into one binary means N sets of file-scope globals.
skyroads-sdl exports `Cur`, `Time`, `Cars_Seg`, `Road_Dat_store`,
`speed_display_offset` — `Cur` and `Time` will collide with the second game
that defines them, and the failure is a link error at integration time, i.e.
the worst possible moment.

Every non-`static` file-scope symbol carries a per-game prefix (`sky_`).
Enforced by `nm` in CI.

> **Open, needs a spike — do not treat as decided.** Manual prefixing is
> ~80 renames for skyroads and pure burden for future porters. The obvious
> automation, `objcopy --prefix-symbols`, was checked on the target dev
> machine and **neither `objcopy` nor `llvm-objcopy` is present, including via
> `xcrun`** — so it would add a toolchain dependency to all three platforms.
> A promising tool-light alternative is generating a force-included
> `-include game_prefix.h` of `#define`s from `nm` output (`nm` *is* universally
> available). That is unverified. Until it is, the MUST above stands as
> source-level prefixing.

## 4. What a porter actually writes

`dxm_entry.c`, and nothing else DXM-specific:

```c
#include "dxm_core.h"
static const dxm_mode MODE_13H = { 320, 200, DXM_FB_INDEX8, 6, 5, 400 };

static const dxm_core_info INFO = {
    .id = "skyroads", .exe_name = "SKYROADS.EXE", .title = "SkyRoads",
    .publisher = "BlueMoon Software", .year = 1993,
    .modes = &MODE_13H, .n_modes = 1,
    .data_probe = "roads.lzs",
};
const dxm_core_info *sky_core_info(void) { return &INFO; }

int sky_core_main(const dxm_host *host, const char *data_dir) {
    sky_reset_state();                    /* §3.2 */
    if (setjmp(*plat_exit_target())) return 0;   /* §3.1 */
    sky_run();                            /* the game's own top-level flow */
    return 0;
}
```

## 5. Conformance harness

`dxm-conformance <core>` — run in CI by every game repo, so a port cannot
regress into unembeddability between releases:

1. **Symbol audit.** `nm` finds no `SDL_*`, no `exit`/`atexit`/`signal`, no
   `getenv`, no unprefixed file-scope globals (§3.5, §3.8).
   *Reference implementation result:* `nm -u libskyroads_core.a` reports **0**
   undefined `SDL_*` symbols and no bare `exit` — only `_plat_exit`.
2. **Boot.** Core reaches first `present()` within 5 s; declared mode matches
   a mode in `info->modes`.
3. **Quit.** `should_quit()` raised at an arbitrary frame; core returns within
   2 s. Process still alive — this is the §3.1 regression test.
4. **Restart.** Run the whole sequence again in the same process; frame 1 must
   be byte-identical to the first run's frame 1 (§3.2).
5. **Isolation.** cwd set to an empty temp dir, environment cleared,
   `data_dir` at a non-obvious path (§3.4, §3.7).
6. **Mode sweep.** Every declared mode is exercised and its `crt_lines` and
   PAR are sane for its dimensions (§2.1, §2.2).

A port passing all six embeds without further work. That is the whole point of
writing this before game #2 rather than after.
