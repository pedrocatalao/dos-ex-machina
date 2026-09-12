# DOS ex Machina — Specification

Version 2. What the machine is, the principles it keeps, and the reasoning
behind the parts that are not obvious from the code. [ARCHITECTURE.md](
ARCHITECTURE.md) is the map of where things are; this is why they are that
way. The code cites sections of this document by number, so the numbering
is kept stable.

## 1. Vision

A standalone, always-fullscreen application for macOS, Linux and Windows
that puts a complete 1993 PC on your screen: a beige case and CRT monitor
drawn as one machine, the monitor's curved tube a region of the display. The
machine powers on, degausses, warms up and runs its POST, then boots a real
DOS, DOSBox, whose picture is shown through the same tube. From the prompt it
is a DOS PC. `EXIT` powers it down, and switching it off is the only way
back to the modern desktop.

**The fiction holds.** No windows, no menus, no dialogs in the way of the
machine. Everything the machine does, it does as the machine: the drive
runs when a program loads, the plastic takes the light of the picture, the
raster collapses when it is switched off.

## 2. Product principles

### 2.1 Fiction first

The machine is the interface. There is no chrome around it, no settings
screen in front of it, no pointer over its glass while it holds the mouse.
Controls that a real machine had are on the case: the power button, the
brightness and contrast knobs, the turbo display and its buttons.

SETUP (§6.9) is not an exception to this: a machine of the period had a
setup screen of its own, reached from the POST, and this one is reached the
same way and drawn as a program of its day.

One exception is deliberate: the Shift+F1 panel, which exposes every CRT
parameter as a slider. It is a tuning surface, outside the fiction, for the
parameters a real set would have had behind a service door. It is opened by
a chord no DOS program would use, it saves to a file, and nothing needs it.

### 2.2 The DOS is real

Version 1 ran native C ports of DOS games behind a simulated prompt. Version
2 runs DOSBox: a real DOS, running real DOS software, unchanged. The machine
does not pretend to be a DOS and does not interpret one; it is the hardware
around one. What it takes from DOSBox is what came down a monitor cable and
out of a speaker jack, a framebuffer and audio, plus the keys, the mouse
and one line of state: that DOS said EXIT.

### 2.3 Drawn, not painted

The case is drawn from parameters at the display's own resolution (§6.1).
There is no raster art of the machine anywhere, and no layout that stretches
a picture of one.

### 2.4 Authentic by default

The shipped settings mimic a used 1993 VGA monitor: a little persistence,
a little bloom, faint static, an imperfect sync. Every effect can be turned
down, but the defaults are tuned by eye for a machine that has been used,
not for flattery.

## 3. Platforms & distribution

macOS universal (Apple Silicon and Intel) as an ad-hoc signed `.app`;
Windows x86_64 and arm64 as a zip; Linux x86_64 and arm64 as a tarball with
an optional per-user installer. Every download is self-contained: SDL3
travels with it where the system does not have it, and the DOSBox core
always does, with its licence. Linux builds on Ubuntu 22.04, so the binary
runs on distributions with that glibc or newer.

The graphics requirement is an OpenGL 3.3 core context (§12.1). The machine
checks for every entry point it needs and says on screen which are missing
rather than crashing.

## 4. Architecture overview

```
+-------------------------------------------------------------------+
| dxm - one window, one GL context, one audio device                |
|                                                                   |
|  main loop     events, time, the POST, the handover, the theatre, |
|                the tube source, the draw                          |
|  chassis/      the case, drawn once on a worker thread            |
|  gpu           the tube pipeline and the lit case (OpenGL 3.3)    |
|  dos/          the machine's own POST, on a text screen           |
|  dosbox/       the bridge to the DOS                              |
|  sound         the machine's own noises                           |
+-------------------------------------------------------------------+
        | libretro API, opened at run time
+-------v-----------------------------------------------------------+
| dosbox_pure_libretro - DOSBox Pure, on its own thread             |
| the whole PC: CPU, VGA, sound cards, COMMAND.COM; C: mounted       |
+-------------------------------------------------------------------+
```

Four threads, each owning its own state:

- **The main thread** owns the window, the GL context, the events and the
  frame loop.
- **The chassis worker** draws the case once at startup, behind the splash,
  and is joined before the first machine frame.
- **The DOSBox thread** runs the core, one `retro_run()` per emulated
  refresh, paced by the core's own reported refresh rate, and converts each
  frame into a triple buffer.
- **The audio callback** pulls DOSBox's samples from a ring and mixes the
  machine's sounds over them.

Between the DOSBox thread and the rest there are two mutexes, one for
frames, keys and mouse, one for the audio ring, and a few shared flags.
Nothing in the main loop waits on the emulator's progress.

## 5. Repo layout

The layout as built is in [ARCHITECTURE.md](ARCHITECTURE.md), which is kept
true.

## 6. The scene & tube

### 6.1 Procedural, not painted

There is no raster art of the machine. The case, the bezel, the speaker
pods, the louvres, the vents, the LEDs, the floppy drive and the lettering
are drawn from parameters at startup, at whatever resolution the display
has. The only images are what a real case carried: the MULTIMEDIA sticker,
and two marks cut into the plastic from their artwork's alpha rather than
laid on top, the DXM mark and the dotted mark.

- **Resolution independence.** A painting is soft on a 5K panel and there is
  no resolution one raster covers from 1080p up. Geometry drawn at the
  native size is crisp everywhere.
- **Aspect independence.** No 9-slice seams and no stretched photograph on a
  16:10 or 21:9 display (§6.3).
- **The case is live where it has to be.** The LEDs, the turbo display's
  digits, the knobs and the light from the picture are drawn over the baked
  case every frame; the case holds only their unlit state.
- **No art pipeline.** The machine is a file of constants (`params.h`),
  tunable in one place.

The cost is paid once. The case renders into an RGBA image at startup and on
a change of resolution; per frame it is one texture. Its alpha channel is
not transparency: it records how much each pixel faces the tube, which the
shader uses to light it (§6.6).

### 6.2 Drawing primitives

No vector library and no platform graphics API: Cairo, Skia, CoreGraphics and
Direct2D are three more build problems and three different rasterisers.
The case is drawn on the CPU, in plain C, from a small set of primitives:
rounded boxes and signed distance fields, bevels, chamfers, soft edges, a
lit height field for the plastic's grain, and an integer hash for every bit
of noise, spelled in unsigned arithmetic so it is the same on every
compiler.

All lettering uses compiled-in bitmap fonts: an 8x8 face for the moulded
and printed lettering on the case, and the VGA 8x16 face for text on the
tube. No system fonts anywhere: CoreText, fontconfig and GDI disagree on
hinting and placement, and there is no configuration that makes them agree.

### 6.3 Layout

Always fullscreen, any resolution, any aspect. **The machine grows, it never
stretches.** Extra width buys more machine, never a wider one.

The solve is tube-first:

1. **The tube** is 4:3, about three quarters of the display's height less
   a margin. Its picture is shown at 4:3 whatever its pixel size, which is
   the VGA pixel-aspect correction: 320x200 and 640x400 displayed 4:3, not
   16:10.
2. **The monitor housing** wraps the tube by a fixed fraction of its height
   on every side, with the dished bezel band inside it.
3. **The speaker columns** take what is left on either side, each a raised
   pod with its grille, the dotted mark engraved above the left one and the
   MULTIMEDIA sticker under the right one's holes. On a display too narrow
   for them, the tube shrinks rather than the speakers vanishing.
4. **The base band** below holds, from the left, the power button and its
   LED, the turbo module, the DXM mark engraved on the centre line, the
   vents and the floppy drive, at real-world sizes in millimetres. The
   sticker and the marks are left off where they do not fit.
5. **The knobs** go under the right speaker when there is room, and on the
   band beside the turbo module when there is not.

One arrangement serves every aspect. Every real-world size is in
millimetres of one scale tied to the display's height, so a wide display
gets more case around the same objects rather than bigger objects, and all
lettering shares one scale, so the machine reads as one product at every
resolution.

**HiDPI.** Everything is sized from the drawable's size in pixels, never the
window's, which differs by the backing scale on macOS and under fractional
scaling on Wayland.

### 6.4 The tube pipeline

The source is an RGB8 image of whatever size the DOS is drawing, with its
physical line count: a 320x200 mode was scanned as 400 lines on a VGA, so
it is drawn with 400. Everything below runs in linear light; the sRGB encode
happens once, at the end.

| # | Pass | Target |
|---|---|---|
| 0 | Upload | the source's own size |
| 1 | Phosphor persistence: a decaying accumulator, green longest (P22) | the source's size, mipmapped |
| 2 | Edge profiles: the light at each of the picture's four edges, weighted by distance from it | 96 x 4 |
| 3 | Ease: the edge light follows the picture with a tenth of a second of inertia | 96 x 4 |
| 4 | Glow field: every point of every edge an emitter, summed over the case | 128 x 96 |
| 5 | Burn-in: a far slower average of the same signal | the source's size |
| 6 | Bloom: separable blur at a fixed size, twice | fixed |
| 7 | Composite: curvature, beam, mask, bloom, glass, the lit case, the LEDs and digits, encode | the display |

The composite, per output pixel: the inverse barrel map into the picture;
the beam profile integrated over the pixel's footprint across the scanlines;
the aperture-grille mask pinned to output pixels; bloom added back; the
glass's static, jitter, flicker, sync and convergence errors; and outside
the aperture, the case, lit by the room and by the glow field.

Three ordering rules that are easy to get wrong:

- **The mask and scanlines come after curvature, in output space.** Warping
  the mask with the picture moirés, and the phosphor grid belongs to the
  glass; the picture bends behind it.
- **Bloom is fed from the picture before the mask,** so glow radiates from
  beam energy rather than from the mask's gaps.
- **Scanlines are modulated in linear light.** In gamma space they read far
  too dark.

The beam is integrated over each output pixel's footprint rather than
point-sampled. Without that, scanlines alias into moiré whenever the tube's
height is not a multiple of the line count, which is almost always.

The persistence and burn-in targets follow the source's size. A fixed-size
target resamples every other mode onto its grid and reads it back as its
own size, which is what once compressed every picture that was not 640x400.

### 6.5 Shaders, and one file that knows the API

Scanlines, the mask and the beam are per-pixel functions of sub-pixel
position; persistence and burn-in need ping-pong targets with decay maths.
None of that is expressible without shaders. So: **one hand-written shader
pipeline, one source, every platform,** in GLSL under `shaders/`, baked into
the binary at build time. `gpu.c` is the only file that names a graphics
API; the rest of the machine hands it images and parameters.

### 6.6 Light on the plastic

The glow from the tube onto the case is where the procedural chassis earns
its keep. The case knows its own geometry, so the light can respect it.

- **The source** is the picture at its edges, not its average. Each edge's
  profile weights the picture by distance from that edge, so a bright line
  at the bottom of the screen lights the bottom of the bezel, and a single
  bright line in the middle lights none of it strongly.
- **The field** treats every point along every edge as an emitter falling
  off with distance, summed over the whole case, so the light is diffuse
  and continuous rather than four separate bands.
- **Inertia.** The edge light eases toward the picture over a tenth of a
  second, so text scrolling past moves the glow smoothly instead of in
  steps.
- **Geometry.** The bezel's shoulder, its fillet and its aperture are one
  set of constants (`crt.h`), shared by the case and the shader, so the
  light drops off exactly where the moulding turns away from the glass, and
  only the plastic that faces the tube takes it (the alpha of §6.1).

### 6.7 Faithfully equal on macOS, Linux and Windows

Bit-identical GPU output is not attainable: the variation between GPU
vendors is larger than the variation between operating systems. The target
is no perceptible difference, reached by removing every source of variation
the machine controls:

- **One shader source, one API.** No per-platform effect code.
- **Time-based, never frame-based.** Every decay is a half-life in seconds,
  applied per frame's elapsed time: persistence, burn-in, the glow's
  inertia, the LED segments' fade, the warm-up and the power-off. A 120 Hz
  display draws the same trails as a 60 Hz one.
- **Fixed internal sizes** for the bloom and the glow field, so their reach
  does not change with the display.
- **The case is plain C.** Drawn on the CPU with integer hashes and bitmap
  fonts, it is the same bytes everywhere, whatever the GPU.
- **Colour.** Linear throughout, one encode at the end. macOS composites
  through the display's colour profile and the others do not; that
  difference is accepted (§12).
- **Golden frames per renderer.** The golden test compares frames pixel for
  pixel, but only against references drawn by the same kind of GPU and
  driver: one set from an Apple GPU, one from Mesa's software renderer in
  CI. It captures the end of the POST under a fixed clock and the shipped
  settings, where the machine draws everything itself; after the handover
  the picture is DOSBox's, on its own clock, and a boot test checks that
  half instead.

### 6.8 Controls

**The knobs.** Brightness and contrast, turned by dragging with the mouse
released to the desktop. They are the one part of the case that moves: each
is drawn over the finished plastic and redrawn alone when turned.

**The turbo module.** A display and three buttons beside the power switch.
The display shows the CPU clock in MHz, or with MODE the frames per second
the machine draws; an LED against the printed legend says which. − and +
step the clock through period speeds, each set in DOSBox as a cycle count
(§7). The segments and the mode LEDs cross-fade over a fifth of a second
rather than snapping.

**The panel.** Shift+F1 opens every CRT parameter as a slider over the tube
(§2.1). Its values, and the knobs', persist in `crt.cfg` in the preferences
directory. Under `--deterministic` the file is neither read nor written, so
a golden frame never measures somebody's contrast setting. It is a tuning
surface, and SETUP (§6.9) has taken over what it is for; it goes when the
OSD replaces it.

**The mouse** is the machine's while it holds it: SDL's relative mode, no
pointer, motion to DOSBox. Ctrl+F10 releases it to the desktop, as in
DOSBox, where it wears a period arrow and works the knobs and buttons. On a
Mac, whose F10 is a media key, a tap of Command alone does the same.

### 6.9 SETUP

The machine's own configuration, and a program in its own right rather than
a panel over the tube: 640x400 in 256 colours, which is the geometry of the
text screen, so the tube treats it exactly as it treats the POST and DOS.
SPACE during the POST opens it, as the POST says; so does `SETUP` at the DOS
prompt, which is a program the fork adds (§7) that stands still while the
screen is up. The machine's clock stops with it - a BIOS setup halted the
boot - but the tube's does not, since its noise and flicker belong to the
glass.

**Indexed, not RGB.** Entries 0..15 are the interface's, the VGA sixteen;
16..255 belong to the artwork across the top, which brings its own palette.
That constraint shapes the screen rather than being worked around: the panel
is a dithered scrim because indices cannot be blended, the fade up is a
palette ramp because that is the only fade such a screen has, and a change
of picture comes in through blinds because two pictures on the glass at once
have to share the 240 colours - which they are fitted to, as a pair, when
the change begins.

**The artwork** travels as the files it was drawn as and is opened when
wanted (`src/setup/banner.c`, stb_image): a third of what the same pictures
cost as pixels, and a decoder the catalogue will want anyway. Opening one
means decoding it, shading it - bright to the waist, then away to black so
it dissolves into the screen - and fitting it 240 colours by median cut,
which is what the 750 ms of loading screen is actually doing.

**Live, or at the next power-on.** The tube's settings are floats the shader
reads every frame, so a slider moves the picture as it is dragged. Nothing
else is: memory, the processor core, the keyboard layout and the MIDI device
are all read once as the core starts, and cannot change under a running DOS
any more than a real machine could be re-chipped while it was on. So the way
out is SAVE & REBOOT, which keeps both files and restarts the machine: the
core goes down, the POST runs again, DOS comes up on the new settings. It is
a restart and not a power cycle - no relay, no degauss, no fade from black,
and the turbo display keeps its clock.

**What is kept.** The tube's settings in `crt.cfg`, the machine's in
`dxm.cfg`, both in the preferences directory, and neither read under
`--deterministic`.

## 7. The DOS

**DOSBox Pure** is the DOS: a libretro core, built from a pinned fork and
shipped beside the program. It is chosen over upstream DOSBox and its other
forks because it is a library by design, with no window, event loop or audio
device of its own, and because it boots straight into a mounted folder.

**Boot behind the POST.** The core is started before the POST is drawn and
is at its prompt before the memory count finishes. The POST draws the
BIOS's first screen and clears it, and at that moment DOSBox takes the
tube. The POST's text screen and a DOSBox text mode are the same size, in
the same font, with the same border, so nothing moves.

**The BIOS's second screen belongs to the emulator.** The System
Configurations box, the pauses between it and the boot, the drive that runs
in them and "Starting DXM-DOS..." are printed by `Z:\DXMBIOS.COM`, a
program in the fork (`dosbox_pure_dxm.h`) that runs as the first line of
the boot. Two reasons it is there rather than in a batch file on C:. A
batch file prints its lines in a few milliseconds, so nothing takes any
time and nothing reads as loading; and the boot should leave nothing lying
on the user's disk. It prints only once the machine says the tube is
showing it, since the emulator reaches its prompt seconds before the POST
ends.

**The greeting** is printed by `DOSBOX.BAT`, which the machine writes into
the root of C: and DOSBox Pure runs instead of its start menu, under that
screen rather than clearing it. The machine rewrites it at each boot for as
long as it carries the machine's marker; a file somebody replaced is
theirs.

**What the machine asks of the core**, through the libretro option
callbacks: the device's sample rate, so nothing is resampled; no start
menu; no save states; no on-screen keyboard; the software Voodoo, since the
core must never want a GL context of its own; and the CPU speed.

**The CPU speed** is set by the turbo display in DOSBox cycles, the
emulated instructions per millisecond, at roughly the machine each clock
names:

| MHz | 33 | 40 | 50 | 66 | 80 | 100 |
|---|---|---|---|---|---|---|
| cycles | 10000 | 14000 | 20000 | 30000 | 40000 | 60000 |

A change takes effect on the core's next frame. The core is told the options
changed by the frontend's "variables updated" flag, polled every frame; the
machine declines the core's offer of an update callback, which would stop
that polling.

**The CPU core.** DOSBox's recompiler is used everywhere but Apple Silicon,
where it cannot allocate executable memory the way it does; there the
interpreter runs, which is ample for the DOS era. The fix is a small patch
to the fork (§12.2).

**EXIT** reaches the machine as the core asking to shut down, and the
machine powers off: relay, fans spinning down, the raster collapsing.

**The drive.** When DOSBox's picture changes size, a program has started or
ended, and the floppy drive runs: its LED, its motor and its stepper.
The first size seen, the prompt's own, does not count, and a drive already
running is not restarted.

## 8. The C: drive

The machine owns a C: drive: a folder in its preferences directory, created
on first run, mounted by DOSBox as C:. DOS software is put there as it sat
on a hard disk. `--dosbox DIR` mounts another folder instead.

The machine writes one file there, `DOSBOX.BAT` (§7). ROM files for an MT-32
or an SC-55 go in the root, where DOSBox Pure finds them and puts the device
on the MPU-401.

No software ships with the machine and nothing is downloaded. Version 1 had
a catalogue that installed native ports; its successor, if there is one,
would install DOS software and its settings onto C: from the publishers'
own downloads.

## 9. Audio

One audio device, 44.1 kHz, 16-bit stereo, opened by the machine. DOSBox's
samples are pulled from a ring into the device's callback at the same rate,
so nothing is resampled, and are silent until the handover. The machine's
own sounds are mixed on top: the mains relay, the degauss thump, the fans
and the disk spindle, the POST beep on the PC speaker, and the floppy
drive. Sound Blaster, AdLib, General MIDI and
the rest are DOSBox's.

## 10. Roadmap

Done in 2.0: DOSBox as the only DOS, bundled on every platform; the machine's
own C: drive; the turbo display wired to the CPU speed; golden frames of the
POST and a boot test through a DOS command.

Next, roughly in order:

- **The website**, rewritten for 2.0 with the release.
- **The recompiler on Apple Silicon** (§12.2).
- **The drive LED from the emulated drive** rather than from the picture,
  which needs the core to report drive activity.
- **A way to install software onto C:** from its publisher's own download,
  with the settings it wants: the sound card, the MIDI device, the speed.
- **A bridge between the machine and DOS** for anything that needs one,
  through files on C: before any change to the core.

## 11. Non-goals, and the dev flags

- Windowed mode, except the `--windowed` dev flag.
- A settings screen in the fiction. The panel (§2.1) is the one tuning
  surface.
- Save states, netplay, gamepads, and more than one DOS at once.
- Shipping any DOS software or data.
- Emulating anything itself. The machine draws, sounds and holds the DOS;
  DOSBox emulates.

The dev flags are the exceptions to the appliance: `--windowed`, `--size`,
`--deterministic`, `--shot` and `--frames`, `--type`, `--ambient`,
`--dump-audio`, `--dosbox` and `--dosbox-core`. F5 and F6 during the POST
darken and lighten the room, for tuning.

## 12. Decisions, and open questions

### 12.1 Graphics API: OpenGL 3.3 core

The machine is built on OpenGL 3.3 core, which every desktop it targets
provides, and which needs no shader cross-compiler; on macOS it is
deprecated but present. The earlier plan was SDL3's GPU API, which compiles
shaders once, offline, and would narrow the differences between drivers.
It remains an option rather than a plan: the renderer-specific golden sets
(§6.7) measure those differences instead, and keeping the API to one file
(§6.5) keeps a move contained, should Apple remove GL.

Off macOS only GL 1.1 is exported by the system library, so every later entry
point is fetched from the driver at run time from one list
(`glfuncs.h`); a missing one is reported by name.

### 12.2 The fork

The fork of DOSBox Pure carries no changes: it is upstream, pinned, and
fast-forwarded when upstream moves. Every difference from upstream is
something to carry, so a change goes into the fork only when a feature cannot
be done from outside, and is written small enough to offer upstream. The
first candidate is the recompiler on Apple Silicon: allocating its code cache
with `MAP_JIT` and toggling write protection around code generation, which
the machine's signed bundle can grant itself the entitlement for.

### 12.3 Licence

GPL-2.0-or-later from 2.0. The one required component is GPL-2.0, and the
machine is the work; version 1.0 and earlier stay MIT.

### Open

- **Colour management on macOS.** Request a specific colour space for the
  window, so all three platforms feed the panel the same numbers, or accept
  the display profile and look right for that display. These are different
  definitions of faithful, and the choice should be deliberate.
- **Burn-in across sessions.** Reset each launch, as now, or accumulate in
  the preferences directory so a long-lived machine slowly acquires a
  permanent `C:\>` ghost.
- **Pacing.** The machine draws at the display's refresh; DOS modes scan at
  70 Hz. A display that is not a multiple of the DOS's rate shows the same
  judder a real capture of it would. Whether to pace the tube to the DOS's
  rate on displays that can is open.
