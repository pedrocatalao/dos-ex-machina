# Code audit — DOS ex Machina 0.4

Scope: the host application in this repository (`src/`, `tools/`, build and
CI). Not in scope: game ports, which live in their own repositories and are
governed by PORTING.md.

Method: line and function metrics; a build under stricter warnings
(`-Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wvla
-Wdouble-promotion -Wcast-qual -Wimplicit-fallthrough -Wswitch-enum -Wundef
-Wformat=2`, clang 17); an allocation and string-handling pass; a read of
the four largest files' internal structure; a comparison of SPEC.md §5
against the tree; CI and test coverage.

## 1. Verdict

The engineering underneath is sound. The GL layer is isolated in one file,
the module contract is explicit and versioned, every download is hashed
before it is executed, the code compiles with no warnings under `-Wall
-Wextra` on clang and GCC 15, there are no unsafe string calls, and the
comments explain *why* to a degree most codebases never reach.

What a senior reviewer will object to, in order of how fast they will see it:

1. **Four files carry 70 % of the hand-written code**, and three functions
   are 500–660 lines long. `chassis_render()` is the whole machine in one
   body; `main()` is argument parsing, logging, GL setup, splash, audio,
   the power theatre and the frame loop in one body; `dos.c` is a terminal,
   a shell, a file pager, a boot sequence and a two-pane file manager in one
   translation unit.
2. **Two formatting styles coexist.** Files written early (`main.c`,
   `dos.c`, `chassis.c`) are dense: `a=b;c=d;` and up to three statements
   per line. Files written later (`catalog.c`, `png.c`, `unzip.c`, `net.c`)
   are conventionally spaced. There is no `.clang-format`.
3. **5.6 MB of the 6.8 MB in `src/` is generated data** sitting beside the
   code it is unrelated to, and the vendored porting adapter
   (`platform_dxm.c`) lives in `src/` while being deliberately excluded from
   the build.
4. **No tests, and CI never runs the binary.** `--selftest` exists and is
   run by nobody. The signed-overflow bug that hung Linux and Windows for a
   week is exactly what a UBSan job running `--selftest` would have caught
   on the first push.
5. **SPEC.md §5 describes a repository that does not exist** (`layout.c`,
   `tube.c`, `fx.c`, `knobs.c`, `shaders/`, `tests/golden/`,
   `make_mac.sh`). A reader who trusts it is misled immediately.

None of this is structural rot; it is the shape of code that grew fast under
one author. The fix is a sequence of behaviour-neutral moves, each verified
pixel-for-pixel against the current output, and it can be done in about a
dozen reviewable commits.

## 2. Metrics

Hand-written C, excluding generated files:

| File | Lines | Largest function | Exported / static functions |
|---|---:|---:|---:|
| `chassis.c` | 2125 | `chassis_render` 658 | 3 / 27 |
| `dos.c` | 1136 | `nc_draw` 230 | 10 / 27 |
| `gpu.c` | 767 | `gl_load` 392 (X-macro table) | 17 / 7 |
| `main.c` | 643 | `main` 506 | 1 / 7 |
| `catalog.c` | 283 | 65 | 8 / 10 |
| `font.c` | 271 | table | 1 / 0 |
| `library.c` | 231 | | 11 / 4 |
| `sound.c` | 201 | `snd_mix` 107 | 9 / 1 |
| other 11 files | 80–170 each | | |

Other numbers:

| | |
|---|---:|
| Functions over 100 lines | 9 |
| File-scope mutable statics (all modules) | 61 |
| Lines in `chassis.c` with three or more float literals | 176 |
| Shader source embedded as C string literals in `gpu.c` | 286 lines |
| Warnings under the stricter flag set | 20 |
| Unchecked allocations | 5 |
| Unsafe string calls (`strcpy`, `strcat`, `sprintf`, `gets`) | 0 |
| Header files without an include guard | 1 (`glfuncs.h`, intentional X-macro, uncommented) |
| Automated tests | 0 |

## 3. Findings

### 3.1 Structure

**F1. `chassis.c` (2125 lines) mixes three layers.** Pixel primitives and
shading (`px_set`, `px_blend`, `px_shade`, noise, `rr_sd`, `text`), part
drawers (`led`, `floppy_drive`, `rotary`, `sb_sticker`, `bezel`,
`vent_slot`, `grille_panel`, `seam`, `engrave_*`), and the assembly
(`chassis_layout`, `chassis_render`). `chassis_render` itself is 658 lines
of sequential sections marked by comments: the band, the badge, the power
button, the LED, the knobs, the floppy, the vents, the wear passes. Each
section is a function waiting to be named.

**F2. `main()` is 506 lines.** It contains argument parsing, the log file,
window and GL creation with their failure dialogs, the splash worker and
its loop, audio device setup, the CRT defaults and `crt.cfg`, the catalogue
and library init, the power-on and power-off theatre state machine, input
translation, the knob drag and mouse-capture logic, and the frame loop.
Reading the frame loop means scrolling past all of the above.

**F3. `dos.c` is five things.** A text-mode terminal (screen buffer,
`put`, `oline`, `scroll`), a pager (`page_*`), a command shell (`run`,
`cmd_dir`, `cmd_help`), the boot sequence (`dos_update`'s POST and memory
count, `draw_badge`), and the navigator (`nc_*`, 600 lines: rows, table
drawing, art blit, dialogs, key handling). They share thirteen file-scope
statics. `nc_draw` alone is 230 lines.

**F4. Shaders are C string literals.** 286 lines of GLSL inside `gpu.c`,
one string per line with quotes and `\n`. No syntax highlighting, no
diffable shader history, and edits require touching the C file. The
X-macro GL loader in `glfuncs.h` is a good pattern and should stay; the
shaders should be `.glsl` files baked into a generated header at build
time, which keeps the single-binary property.

**F5. Generated data lives among the code, and most of it does not
reproduce.** Seven generated headers and `dxm_splash.c` (1.4 MB) sit in
`src/` beside the hand-written files, all marked "do not edit" but with no
single script that regenerates them and no check that the committed output
matches the tool. Trying to regenerate them shows why that check matters:
only the splash and the DXM mark come out byte-identical from `assets/`.
The wordmark, icon, road and corner sticker were made by an earlier
`mklogo.py` that scaled differently, and the Sound Blaster sticker and the
floppy PCM have no source in the repository at all. Those six are frozen
bytes until someone decides to regenerate them, looks at the result, and
re-blesses the golden frames; the decision is recorded in
`src/gen/README.md` rather than made silently.

**F6. The porting contract is not separated from the host.** `dxm_core.h`
and `platform_dxm.c` are the two files a port vendors. One is built into
the host, the other is deliberately not, and both sit in `src/` with a
CMake comment explaining the exception. They should be in their own
directory (`contract/`) so that "what a port needs" is a directory, not a
paragraph.

**F7. SPEC.md §5 is wrong.** It lists twelve source files, a `shaders/`
directory with SDL_shadercross output, `tests/golden/`, and shell build
scripts; none exist, and the actual modules (`library`, `install`, `net`,
`unzip`, `png`, `art`, `disk`, `ui`, `coreload`) are absent. SPEC is
otherwise a good design document and should be kept as one, with §5
replaced by a short, true architecture note.

**F8. Duplicated `screenshots/`.** 5.5 MB of PNGs from early builds
(`dxm-fullscreen-3456x2160.png`, `dxm-stereo-2560x1080.png`) tracked in git
beside the current `docs/screenshot-*.jpg`. Stale and heavy.

### 3.2 Code

**F9. Two formatting styles.** Measured by lines with more than one
statement: `dos.c` 104, `chassis.c` 83, `gpu.c` 61, `main.c` 51,
`catalog.c` 4. Measured by unspaced assignment (`a=b`): `chassis.c` 673,
`dos.c` 223, versus `catalog.c` 0. Neither style is wrong; having both is
the tell of code written in sessions rather than designed. One
`.clang-format`, applied once in a single commit, ends it.

**F10. Four unchecked allocations.** `chassis.c:1468` (the whole chassis
canvas, W×H×4: 30 MB at 4K), `gpu.c:487` (the GPU object), `gpu.c:694`
(readback), `ui.c:105`. The canvas one deserves the same message-box exit
the GL failure gets; the rest are one-line checks. (The knob background at
`chassis.c:1436`, first counted here, is checked two lines later.)

**F11. Twenty findings under stricter warnings.** Seven `-Wshadow`
(inner variables reusing outer names), four `-Wdouble-promotion` (float
maths silently done in double, which on the chassis path is also the
cross-platform-determinism concern SPEC §6.7 raises), four `-Wcast-qual`,
four `-Wformat-nonliteral` (the log functions), one `-Wswitch-enum`. All
fixable in an hour. After that, those flags belong in CMake and CI should
build with `-Werror`.

**F12. Module state is loose statics.** 61 file-scope mutable variables:
`dos.c` 13, `sound.c` 10, `corehost.c` 9, `install.c` 5, `ui.c` 5. A
single machine justifies singletons, but each module should own one
`static struct` so that what "reset this module" means is visible and so
that the restart guarantee (PORTING §3.2) has something to point at.

**F13. Hand-tuned constants are inline.** `chassis.c` has 176 lines with
three or more float literals. Most are shading curves that belong where
they are. The ones with physical meaning (millimetre dimensions, the light
direction, the corner radius, the badge geometry, the knob tones) were
tuned by eye over many sessions and are the values a maintainer will need
to find again. SPEC promised `chassis_params.h` for exactly this.

**F14. `DXM_VERSION` fallback is duplicated** in `main.c` and `dos.c`.
One `version.h`.

**F15a. Paths are built by bounded `snprintf` and truncation is silent.**
`library.c` joins the preferences directory, `games`, the id and a file
name into 1024-byte buffers a dozen times. `snprintf` bounds the write,
but a path that did not fit is a wrong path, not an error. GCC's
truncation warning fires on every one of these, and is switched off
rather than fixed, because the fix is a join helper that fails loudly and
callers that handle the failure. That belongs with step 7, when module
state is restructured, and step 9 can test it.

**F15. `catalog` vs `catalogue`.** The code says `catalog.c`, `cat_*`; the
file, the docs and the UI say `catalogue`. Pick one; the docs are British
throughout, so the code should follow.

### 3.3 Build, CI, tests

**F16. No test of any kind.** Candidates that need no SDL and no GL:
`sha256` against the published test vectors; `unzip` and `png` against
small fixtures; the catalogue parser against `catalogue.json` and against
malformed input; `disk`; `font`. These are an afternoon with `ctest` and
no framework.

**F17. CI never executes the binary.** Three jobs compile and package.
`--selftest` (launch, unwind, relaunch, twice) is run by hand on macOS
only. A Linux job with Mesa's llvmpipe (GL 4.5 in software, fine for a
test) can run `--selftest` and `--shot` headless. The same job should build
Debug with `-fsanitize=address,undefined`; that is the job that would have
caught the `hash2` overflow.

**F18. No golden frames.** `--shot` at a fixed size is deterministic on a
given platform. A committed reference frame and a tolerance check turns
every chassis refactor into a pixel-exact regression test, and is the
mechanism the rest of this plan relies on.

**F19. `CMakeLists.txt` sets `-O2` by appending to `CMAKE_C_FLAGS`**, so
`CMAKE_BUILD_TYPE=Debug` still optimises. Use `target_compile_options` for
warnings and let the build type own optimisation.

### 3.4 Process

**F20. Some commit messages do not describe their commits.** `245fb96`
is titled "replace road bitmap with procedural badge ribbon rendering" and
touches only `docs/index.html`. Generated summaries need a read before
they are accepted; history is the first thing a reviewer opens.

## 4. Target layout

```
dos-ex-machina/
├── CMakeLists.txt
├── contract/                    what a port vendors, nothing else
│   ├── dxm_core.h
│   └── dxm_platform.c           (today src/platform_dxm.c)
├── src/
│   ├── main.c                   args, init order, frame loop      ≤ 200
│   ├── log.c/.h                 dxm_log, the log file
│   ├── version.h
│   ├── app.c/.h                 window, GL context, failure dialogs
│   ├── splash.c/.h              worker thread + splash loop
│   ├── theatre.c/.h             power-on / power-off state machine
│   ├── input.c/.h               scancode map, mouse capture, knob drag
│   ├── gpu.c/.h                 unchanged in role; shaders moved out
│   ├── glfuncs.h
│   ├── chassis/
│   │   ├── canvas.c/.h          px_*, shading, noise, sd functions, text
│   │   ├── params.h             every physical dimension and colour
│   │   ├── parts.c              led, power, knobs, sticker, badge, floppy
│   │   ├── surface.c            band, seams, vents, grille, wear passes
│   │   ├── bezel.c              aperture and monitor bezel
│   │   ├── layout.c             chassis_layout
│   │   └── chassis.c            assembly: calls the parts in order   ≤ 300
│   ├── dos/
│   │   ├── term.c/.h            screen buffer, cursor, pager
│   │   ├── shell.c/.h           prompt, commands, TYPE/MORE, disk
│   │   ├── boot.c/.h            POST, memory count, badge
│   │   ├── nc.c/.h              the navigator
│   │   └── dos.c/.h             façade: dos_init/dos_key/dos_update
│   ├── catalogue.c/.h  library.c/.h  install.c/.h  net.c/.h
│   ├── corehost.c/.h  coreload.c/.h
│   ├── sound.c/.h  ui.c/.h  font.c/.h  disk.c/.h
│   ├── png.c/.h  unzip.c/.h  sha256.c/.h
│   └── gen/                     generated, committed, reproducible
│       ├── splash.c/.h  mark.h  logo.h  icon.h  road.h  sb_logo.h
│       ├── corner_sticker.h  fdd_pcm.h
│       └── shaders.h            from shaders/*.glsl at build time
├── shaders/
│   ├── composite.frag  persist.frag  blur.frag  ...
├── assets/                      sources for gen/, correctly named
├── tools/
│   ├── regen.sh                 rebuilds everything in src/gen from assets/
│   └── mklogo.py  mksplash.py  mkpcm.py  mkico.py  make_icns.sh
├── tests/
│   ├── CMakeLists.txt
│   ├── test_sha256.c  test_unzip.c  test_png.c  test_catalogue.c ...
│   ├── fixtures/
│   └── golden/                  reference frames + compare script
├── docs/                        the site, unchanged
├── packaging/
├── ARCHITECTURE.md              one page, true, replaces SPEC §5
└── SPEC.md  PORTING.md  README.md  LICENSE
```

Rules the layout enforces: no hand-written file over ~600 lines; no
function over ~120 lines except tables; one `static struct` of state per
module; a subdirectory when a module grows more than three files.

## 5. Plan

Every step is one commit, builds warning-free on clang and GCC, passes
`--selftest`, and produces a frame identical to the baseline. Steps are
ordered so that the safety net exists before anything is moved.

| # | Step | Size | Verifies with |
|---|---|---|---|
| 0 | **Done.** `--deterministic` mode (fixed 60 Hz clock, no HiDPI, shipped CRT defaults, no vsync); `tests/golden/run.py` with three cases (prompt and README pager at 1280×800, prompt at 1720×720); references under `tests/golden/references/macos/`; `ctest` runs it in 9 s | S | itself |
| 1 | **Done.** Strict warning set in `target_compile_options` (clang and GCC 15 clean); `DXM_WERROR` option, on in all three CI jobs; format attributes on the log and message helpers, which became variadic; const-correct icon upload and zlib input; explicit float→double casts; one `if` per line where GCC saw misleading indentation; allocation checks with a message box for the chassis canvas; `version.h`; build type defaults to Release | S | build + golden + `--selftest` |
| 2 | **Done.** Generated data under `src/gen/` (`splash`, `mark`, `logo`, `icon`, `road`, `sb_logo`, `corner_sticker`, `fdd_pcm`), the contract under `contract/` (`dxm_core.h`, `dxm_platform.c`, the name ports use); `tools/regen.sh` with `--check` in the Linux job; `src/gen/README.md` lists each file, its tool, its source and whether it reproduces; `screenshots/` removed. The badge asset was already correctly named; only the stale header comment said otherwise | S | build + golden |
| 3 | **Done.** Eight GLSL files under `shaders/` (`quad.vert` and one `.frag` per pass), each carrying the comment that sat above its literal; `tools/embed.cmake` bakes them into `build/generated/shaders.h` at build time, so editing a shader recompiles `gpu.c` and the binary stays self-contained. `gpu.c` went from 767 to 462 lines | M | golden |
| 4 | Split `chassis.c` into `chassis/` per §4; extract `chassis_render` sections into named functions; `params.h` | L | golden, pixel-exact |
| 5 | Split `dos.c` into `dos/` per §4; one state struct per file | L | golden at prompt and NC frames; manual NC pass |
| 6 | Split `main.c` into `log`, `app`, `splash`, `theatre`, `input` | M | golden + `--selftest` |
| 7 | Per-module state structs elsewhere (`sound`, `corehost`, `install`, `ui`) | S | build + `--selftest` |
| 8 | `.clang-format`; one formatting commit; `clang-format --dry-run` in CI | S | golden (no semantic change) |
| 9 | Unit tests for `sha256`, `unzip`, `png`, `catalogue`, `disk`, `font` | M | ctest |
| 10 | Linux CI job: Debug + ASan/UBSan, llvmpipe, runs `ctest`, `--selftest`, golden compare | M | itself |
| 11 | `ARCHITECTURE.md`; SPEC §5 replaced by a pointer; README build section; `catalog` → `catalogue` | S | docs |

S is under an hour of focused work, M a few hours, L a day with review.
Steps 4 to 6 are the ones worth a second reviewer; the others are
mechanical.

## 6. Decisions (taken 2026-09-06)

1. **Formatting:** spaced style, four-space indent, 100 columns, K&R
   braces, one statement per line. Reference: `catalog.c`, `png.c`,
   `unzip.c`, `net.c`.
2. **Generated files stay committed** under `src/gen/`; CI reruns
   `tools/regen.sh` and fails on any diff.
3. **`catalog` becomes `catalogue`** in code; the `cat_` prefix stays.
4. **Golden frames on macOS first**; Linux references arrive with the
   sanitizer job in step 10.

## 7. What is deliberately left alone

- The comment style. It is unusual and it is the best thing in the code.
- The X-macro GL loader.
- Fixed-size buffers with `snprintf`. Correct as written.
- The singleton design. One machine, one process; a context-object rewrite
  would cost a great deal and buy nothing.
- The tube pipeline and the chassis maths. This is a restructure, not a
  redesign; the golden frames exist to guarantee it.
