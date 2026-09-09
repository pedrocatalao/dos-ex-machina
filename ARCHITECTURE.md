# Architecture

What is where, what each part owns, and how a frame is made. This is the
map; [SPEC.md](SPEC.md) is the reasoning behind the design and
[PORTING.md](PORTING.md) is the contract a game port satisfies.

## The tree

```
dos-ex-machina/
├── src/
│   ├── main.c              option parsing, the init order, the frame loop
│   ├── app.c/.h            SDL, the window, the GL context, the GPU pipeline,
│   │                       the audio device, the clock, screenshots
│   ├── splash.c/.h         the chassis worker thread and the splash it hides behind
│   ├── theatre.c/.h        power on and off, the two LEDs, the fades, the drive
│   ├── input.c/.h          scancodes, mouse capture, knob drag, the event switch
│   ├── selftest.c/.h       --selftest: launch, unwind, relaunch a game
│   ├── log.c/.h            dxm_log: stderr and dxm.log in the preferences dir
│   ├── gpu.c/.h            the ONLY file that knows OpenGL: targets, passes,
│   │                       uniforms, the run-time entry-point loader (glfuncs.h)
│   ├── chassis.h           the public face of src/chassis/: layout, render, knobs
│   ├── chassis/            the machine, drawn procedurally at startup
│   │   ├── chassis.c       the assembly: solves the geometry, calls the passes in order
│   │   ├── layout.c        tube-first layout from the display size
│   │   ├── canvas.c        pixels, grain, shading, distance fields, bevels, lettering
│   │   ├── surface.c       the case body: base coat, roll, side strips, recess, wear, light
│   │   ├── bezel.c         the aperture in warped space, the dished band, the facing mask
│   │   ├── parts.c         LEDs, the power button, vents, grilles, the floppy drive
│   │   ├── marks.c         the badge, the stickers, the engraved marks
│   │   ├── knobs.c         the rotary controls, drawn last and redrawn alone when turned
│   │   ├── params.h        every tuned dimension and colour
│   │   └── internal.h      shared inside src/chassis only
│   ├── dos.h               the public face of src/dos/
│   ├── dos/                the DOS the machine boots into
│   │   ├── dos.c           the façade and the shared machine state
│   │   ├── term.c/.h       the 80x25 text screen; nothing else touches the cells
│   │   ├── shell.c         the prompt, the command set, MORE
│   │   ├── boot.c          POST, the RAM count, AUTOEXEC's echoes, the badge
│   │   ├── nc.c            NC.EXE, the navigator
│   │   └── internal.h
│   ├── dosbox.h            the public face of src/dosbox/
│   ├── dosbox/             a real DOS behind the glass: DOSBox Pure as a libretro core
│   │   ├── dosbox.c        opens the core, runs it on its own thread, frames, keys, audio
│   │   ├── keys.c          SDL scancodes to libretro keys
│   │   ├── libretro.h      the libretro API, vendored verbatim from libretro-common (MIT)
│   │   └── internal.h
│   ├── disk.c/.h           the virtual C:\ - in memory, read-only
│   ├── font.c/.h           the CP437 8x8 glyphs
│   ├── sound.c/.h          the machine's own sounds: fans, spindle, relay, drive, beep
│   ├── corehost.c/.h       runs a game core on its own thread; frames, keys, audio
│   ├── coreload.c/.h       opens a .dxm module (dlopen / LoadLibrary)
│   ├── library.c/.h        the games installed under <prefs>/games/
│   ├── catalogue.c/.h      the list of games that could be installed; parser, cache, refresh
│   ├── install.c/.h        download, verify, unpack, on a worker thread
│   ├── art.c/.h            the navigator's artwork cache
│   ├── net.c/.h            HTTPS through libcurl
│   ├── unzip.c/.h          just enough ZIP for the data archives
│   ├── png.c/.h            just enough PNG for the artwork
│   ├── sha256.c/.h
│   ├── ui.c/.h             the F1 panel: the CRT parameters, crt.cfg
│   ├── crt.h               the barrel geometry, shared by the bezel and the shader
│   ├── version.h
│   └── gen/                generated data, committed; see its README
├── contract/               what a port vendors: dxm_core.h and dxm_platform.c
├── shaders/                one GLSL file per pass, baked into the binary at build time
├── tests/
│   ├── unit/               module tests; a twenty-line check.h, no framework
│   └── golden/             reference frames per renderer, compared pixel for pixel
├── tools/                  regen.sh, format.sh, embed.cmake, the asset bakers
├── packaging/              the macOS bundle, the Windows resource, the Linux installer
├── assets/                 sources for src/gen
├── docs/                   the website
└── catalogue.json          the catalogue, fetched at run time
```

Rules the tree keeps: no hand-written file over about 600 lines; no
function over about 120 lines except tables; one `static struct` of state
per module; a subdirectory once a module has more than three files.

## A frame

`main.c` owns the loop; everything else is called from it, in this order.

1. **Events** go to `input_event()`. Keys reach the running game as XT
   scancodes through `corehost_push_key()`, or the prompt through
   `dos_key()`. The mouse belongs to the machine (locked to the window in
   SDL's relative mode, so no arrow is ever drawn over the glass) or to the
   operating system (the arrow, the knobs); Ctrl+F10 switches.
2. **Time** is `app_now_ns()`: the wall clock, or under `--deterministic`
   a counter advancing one sixtieth of a second per frame.
3. **The core's lifecycle.** A launch the DOS asked for (`dos_launch_request`)
   starts the module on its own thread through `corehost_start()`; a core
   that has exited hands the prompt back through `dos_core_exited()`.
4. **The DOS** advances (`dos_update`): boot, prompt, navigator. It asks for
   sounds by leaving requests (`dos_take_beep`, `dos_take_floppy`) that the
   loop turns into `theatre_drive()` and `snd_beep()`.
5. **The theatre** sets the tube's power state (warm-up, steady, collapse)
   and the two LEDs (`theatre_frame`).
6. **The tube source** is the core's frame if one is running, else
   `dos_render()`: an RGB8 image, with its line count and column count.
7. **The knobs** that moved are redrawn into the chassis texture
   (`chassis_knob_set`, `gpu_patch_chassis`).
8. **The GPU** draws it all (`gpu_draw`): persistence and burn-in passes,
   then one composite pass - curvature, beam and mask, bloom, glass, the
   chassis with the picture's light spilling onto it. The F1 panel goes on
   as an overlay; the room fades come last (`theatre_room`).

The chassis is drawn once, on a worker thread behind the splash, into an
RGBA8 image whose alpha channel says how much each pixel faces the tube.
After that it is one texture, patched only when a knob turns.

## A real DOS (the `dosbox` branch)

`--dosbox DIR` boots DOSBox Pure - opened at run time as a libretro core,
`dosbox_pure_libretro.<dylib|so|dll>` beside the program or in the
preferences directory - with DIR mounted as `C:`.  It boots on its own
thread while the machine's POST plays, and the boot ends on a cleared
screen (`DOS_HANDOVER`) instead of at the simulated prompt; from then on
the tube's source is the core's framebuffer, keys and relative mouse
motion go to it, and the audio device pulls from its ring.  A
`DOSBOX.BAT` the machine writes into the root prints what the machine's
own AUTOEXEC.BAT prints, so the prompt arrives as it always did.  `EXIT`
in that DOS powers the machine off.  Everything downstream of the tube
source - persistence, mask, bloom, the light on the case - is untouched.

In a text mode the tube does not show the core's pixels at all.  The fork
carries one patch beyond the libretro API - `dbp_dxm_text_screen`, a
snapshot of the 80x25 cells and the cursor taken at the end of every
emulated frame - and `main.c` hands those cells to `dos_render_text()`,
the machine's own renderer: same font, same cursor, same border as the
prompt it booted with, so DOSBox's text and the machine's are one thing
on the glass and the handover is invisible by construction.  Graphics
modes, and text geometries the machine does not draw (80x50, 40 columns),
go to the tube as pixels, wearing the text screen's overscan border.
`libretro.h` is vendored as it ships and is the one file under `src/` not
written here.

Bringing a real DOS in also found a fault in the tube that only 320x200
and 640x400 had been hiding: the persistence and burn-in targets were a
fixed 640x400, so every other picture was resampled to that grid and then
read back as if it were its own size.  They follow the source's size now
(`gpu_set_tube`), which is what let 640x480 keep all its rows.

## The contract with a game

A game is a `.dxm` module built from its own repository with the two files
in `contract/` vendored in. `dxm_core.h` is the interface: what the host
provides (frames out, keys and audio in, files, time) and the three symbols
a module exports. `dxm_platform.c` is the adapter every port compiles in.
The eight rules a module must keep are in PORTING.md; `--selftest` checks
the two that matter most, that a core unwinds on request and can be started
again in the same process.

## Verification

- `ctest -L unit` builds each testable module from its own sources with the
  libraries it needs, and runs it. No display required.
- `ctest -L golden` runs the machine under `--deterministic` and compares
  three frames pixel for pixel with the references for this renderer.
  A refactor must leave them identical; a visual change re-blesses them
  on purpose.
- `--selftest` launches, unwinds and relaunches a game.
- The Sanitizers workflow does all three under AddressSanitizer and
  UndefinedBehaviorSanitizer, headless, with a software GL driver. It
  checks the C, which is the same everywhere; the runner it happens to use
  is not the point.
- `tools/regen.sh --check` proves the committed generated data is what
  the tools produce; `tools/format.sh --check` that the formatting holds.
