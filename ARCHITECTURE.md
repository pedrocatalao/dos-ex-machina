# Architecture

What is where, what each part owns, and how a frame is made. This is the
map; [SPEC.md](SPEC.md) is the reasoning behind the design.

## The tree

```
dos-ex-machina/
├── src/
│   ├── main.c              option parsing, the init order, the frame loop
│   ├── app.c/.h            SDL, the window, the GL context, the GPU pipeline,
│   │                       the audio device, the clock, screenshots
│   ├── splash.c/.h         the chassis worker thread and the splash it hides behind
│   ├── theatre.c/.h        power on and off, the LEDs, the turbo display, the fades,
│   │                       the drive
│   ├── input.c/.h          keys, mouse capture, the knobs and buttons, the event switch
│   ├── cursor.c/.h         the period arrow the mouse wears when it is out on the case
│   ├── log.c/.h            dxm_log: stderr and dxm.log in the preferences dir
│   ├── gpu.c/.h            the ONLY file that knows OpenGL: targets, passes,
│   │                       uniforms, the run-time entry-point loader
│   ├── glfuncs.h           the GL 3.3 entry points the loader fetches off macOS
│   ├── chassis.h           the public face of src/chassis/: layout, render, knobs
│   ├── chassis/            the machine, drawn procedurally at startup
│   │   ├── chassis.c       the assembly: solves the geometry, calls the passes in order
│   │   ├── layout.c        tube-first layout from the display size
│   │   ├── canvas.c        pixels, grain, shading, distance fields, bevels, lettering
│   │   ├── surface.c       the case body: base coat, roll, side strips, louvres, recess,
│   │   │                   wear, light
│   │   ├── bezel.c         the aperture in warped space, the dished band, the facing mask
│   │   ├── parts.c         LEDs, the power button, the turbo module, vents, grilles,
│   │   │                   the floppy drive
│   │   ├── marks.c         the MULTIMEDIA sticker and the engraved marks
│   │   ├── knobs.c         the rotary controls, drawn last and redrawn alone when turned
│   │   ├── params.h        every tuned dimension and colour
│   │   └── internal.h      shared inside src/chassis only
│   ├── dos.h               the public face of src/dos/
│   ├── dos/                the machine's own POST
│   │   ├── dos.c           the façade: boot, then handover
│   │   ├── term.c/.h       the 80x25 text screen the POST is drawn on
│   │   ├── boot.c          the BIOS lines, the RAM count, the badge
│   │   └── internal.h
│   ├── setup.h             the public face of src/setup/
│   ├── setup/              the machine's own configuration program
│   │   ├── setup.c         what it holds, what the keys do, the screen it draws
│   │   ├── canvas.c        640x400 in 256 colours: the artwork, text, rules, the scrim
│   │   ├── banner.c        opens a banner (stb_image), shades it, fits it a palette
│   │   ├── machine.c       what the machine is set to, and dxm.cfg
│   │   └── internal.h
│   ├── dosbox.h            the public face of src/dosbox/
│   ├── dosbox/             the DOS: DOSBox Pure as a libretro core
│   │   ├── dosbox.c        opens the core, runs it on its own thread; frames, keys,
│   │   │                   mouse, audio, options, DOSBOX.BAT
│   │   ├── keys.c          SDL scancodes to libretro keys
│   │   ├── motd.c          the message of the day under the greeting
│   │   ├── layout.c        the host's keyboard, as a DOS keyboard layout
│   │   ├── libretro.h      the libretro API, vendored as it ships (MIT)
│   │   └── internal.h
│   ├── segdisp.h           the turbo display's digit geometry and clock stops, shared
│   │                       by the chassis and the shader
│   ├── font.c/.h           the CP437 8x8 lettering font and the VGA 8x16 text font
│   ├── sound.c/.h          the machine's own sounds: fans, spindle, relay, drive, beep
│   ├── ui.c/.h             the Shift+F1 panel: the CRT parameters, crt.cfg
│   ├── crt.h               the barrel and bezel geometry, shared by the case and the shader
│   ├── version.h
│   └── gen/                generated data, committed; see its README
├── src/third_party/        stb_image.h, vendored as it ships: the only
│                           third-party code in the program besides SDL3
├── external/dosbox-pure/   the DOSBox Pure fork, a submodule, built by CMake
├── shaders/                one GLSL file per pass, baked into the binary at build time
├── tests/
│   ├── unit/               module tests; a twenty-line check.h, no framework
│   ├── golden/             the end of the POST, compared pixel for pixel per renderer
│   └── boot/               the whole machine booted through a DOS command and EXIT
├── tools/                  regen.sh, format.sh, embed.cmake, the asset bakers
├── packaging/              the macOS bundle, the Windows resource, the Linux installer
├── assets/                 sources for src/gen
└── docs/                   the website
```

Rules the tree aims to keep: no hand-written file over about 600 lines; no
function over about 120 lines except tables; one `static struct` of state
per module; a subdirectory once a module has more than three files. Three
files are over the first line and due a split: `chassis/parts.c`,
`dosbox/dosbox.c` and `gpu.c`.

## A frame

`main.c` owns the loop; everything else is called from it, in this order.

1. **Events** go to `input_event()`. Once DOSBox has the tube, keys go to it
   as SDL scancodes (`dosbox_key`); during the POST a key only hurries it
   along. The mouse belongs to the machine (locked to the window in SDL's
   relative mode, its motion going to DOSBox) or to the operating system
   (the arrow, the knobs, the turbo buttons); Ctrl+F10 switches. Shift+F1
   opens the panel.
2. **Time** is `app_now_ns()`: the wall clock, or under `--deterministic`
   a counter advancing one sixtieth of a second per frame.
3. **The POST** advances (`dos_update`) until it ends, on a cleared screen
   (`DOS_HANDOVER`). It asks for sounds by leaving requests
   (`dos_take_beep`, `dos_take_floppy`) that the loop turns into
   `snd_beep()` and `theatre_drive()`.
4. **The handover.** When the POST has ended and the core is running,
   `dosbox_show()` gives DOSBox the tube and the speaker. `--type` input is
   fed to it from here, a command a second.
5. **The theatre** sets the tube's power state (warm-up, steady, collapse),
   the LEDs, and the turbo display: the case bakes the window with its
   unlit digits, and the shader lights the segments of the clock or the
   frame rate (`segdisp.h` holds the geometry both sides draw from). A DOS
   that has run EXIT starts the power-off.
6. **The tube source** is SETUP's own screen while it is up (`setup_render`,
   640x400 in 256 colours), else DOSBox's latest frame once it has the tube,
   else the POST's text screen (`dos_render`): an RGB8 image, with its line
   count and column count. SETUP stops the machine's clock while it is
   open - the POST waits where it stood - but not the tube's, whose noise
   and flicker belong to the glass. A change in DOSBox's picture size is a program
   starting or ending, and runs the floppy drive.
7. **The knobs** that moved are redrawn into the chassis texture
   (`chassis_knob_set`, `gpu_patch_chassis`).
8. **The GPU** draws it all (`gpu_draw`): the persistence and burn-in
   passes, the light the picture's edges throw on the case, the bloom, and
   one composite pass: curvature, beam and mask, bloom, glass, and the
   chassis lit by the picture. The panel goes on as an overlay; the room
   fades come last (`theatre_room`).

The chassis is drawn once, on a worker thread behind the splash, into an
RGBA8 image whose alpha channel says how much each pixel faces the tube.
After that it is one texture, patched only when a knob turns.

## The DOS

DOSBox Pure is a libretro core: a shared library,
`dosbox_pure_libretro.<dylib|so|dll>`, opened at run time from beside the
program (in a macOS bundle, from `Contents/Frameworks`), or from
`--dosbox-core`. `src/dosbox/` drives it on a thread of its own, one
`retro_run()` per emulated refresh, and hands the rest of the machine three
things: the latest frame, the audio, and whether DOS has said EXIT.

- **Boot.** The core starts before the POST is drawn, with the machine's C:
  drive mounted: `<prefs>/C`, or `--dosbox DIR`. It is at its prompt long
  before the memory count is done, hidden until the handover.
- **The BIOS's second screen** is drawn inside the emulator, by
  `Z:\DXMBIOS.COM`, a program the fork adds (`dosbox_pure_dxm.h`) and runs
  as the first line of the boot. It prints nothing until the machine says
  the tube is showing it, then puts up the System Configurations box, waits,
  runs the drive, and prints "Starting DXM-DOS...". The pauses are why it
  lives there: a batch file's lines print in a few milliseconds, and
  nothing that takes no time reads as loading.
- **DOSBOX.BAT.** DOSBox Pure runs a `DOSBOX.BAT` it finds in the root in
  place of its own start menu. The machine writes one that prints the
  message of the day and the greeting under that screen, and rewrites it at
  each boot - the message is drawn again every time - for as long as it
  starts with the machine's marker line; a file somebody replaced is left
  alone.
  The message comes from `motd.c`: the shipped set, or a `MOTD.TXT` in the
  root of C: if the user put one there.
- **The picture.** Frames come out as XRGB8888 and are converted to RGB8 on
  the core's thread, into a triple buffer the main loop reads without
  waiting. Text modes wear a border in the same proportion as the POST's
  text screen, so a 640x400 text mode lands on the canvas the BIOS drew on,
  and the handover does not move a pixel. The POST draws with the VGA 8x16
  font (`src/gen/font16.h`), glyph for glyph the one DOSBox draws with, so
  the BIOS lines and the prompt are the same shapes on the same glass.
- **SETUP.** `Z:\SETUP.COM` is a program the fork adds: it asks the machine
  to put its configuration screen up and then stands still, so DOS is doing
  nothing behind a screen that is not DOS's. The machine answers whether it
  is still up, and the command returns when it is not.
- **Keys, mouse and typing.** Keys go in as libretro key events with the
  lock-key state; the mouse as relative motion and buttons, polled by the
  core. `dosbox_type` queues a string that the core's thread types a key a
  frame, for `--type` and the tests. Which national layout DOS translates
  those keys through is `layout.c`'s guess from what SDL says the host's
  keys produce, answered to the core as its keyboard-layout option;
  `--keyboard CODE` forces one.
- **Sound.** The core's samples go into a ring the audio callback pulls
  from, at 44.1 kHz so nothing is resampled; the machine's own sounds are
  mixed on top.
- **Options.** The core asks for its settings through the libretro
  environment callback and gets the machine's answers: no start menu, no
  save states, no on-screen keyboard, the software Voodoo, and the
  interpreter CPU core on Apple Silicon, where the recompiler cannot run.
  The CPU speed is the one option that changes while it runs: the turbo
  display's buttons set it, and the core rereads it on its next frame.
- **EXIT.** DOS's EXIT reaches the machine as the core asking to shut down,
  and the machine powers off.

The core is built from the fork in `external/dosbox-pure` by its own
Makefile, driven from CMake (see the `DXM_CORE` block in `CMakeLists.txt`),
with its objects in the submodule's own ignored build directory, one per
architecture; a universal macOS build joins the two with lipo. The fork
carries one change of its own, `dosbox_pure_dxm.h`: the BIOS program above
and the private libretro environment calls it talks to the machine
through - who is in front of it, the turbo display's clock, whether the
tube is showing this screen yet, and the drive. A core built from upstream
never asks, and the machine copes.

## Verification

- `ctest -L unit` builds each testable module from its own sources and runs
  it. No display required.
- `ctest -L golden` runs the machine under `--deterministic` and compares
  the end of the POST, at two display sizes, pixel for pixel with the
  references for this renderer. Everything up to the handover is the
  machine's own drawing on the fixed clock, so it is exact. A refactor must
  leave the frames identical; a visual change re-blesses them on purpose.
- `ctest -L boot` boots the whole machine, types MEM and EXIT, and checks
  from the log that the core booted, DOSBox took the tube, MEM ran and the
  machine powered off. That is the half the golden frames cannot see, since
  DOSBox's picture runs on its own clock.
- The Sanitizers workflow runs all three under AddressSanitizer and
  UndefinedBehaviorSanitizer, headless, with a software GL driver. The core
  itself is not sanitized; everything of DXM's that talks to it is.
- The three platform workflows build the machine and the core for x86_64
  and arm64 and package both; macOS and Linux also check that the core
  exports `retro_run`.
- `tools/regen.sh --check` proves the committed generated data is what the
  tools produce; `tools/format.sh --check` that the formatting holds.
