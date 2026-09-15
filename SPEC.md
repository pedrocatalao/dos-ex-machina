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
brightness and contrast knobs, the turbo display and its buttons, the OSD
button.

SETUP (§6.9) is not an exception to this: a machine of the period had a
setup screen of its own, reached from the POST, and this one is reached the
same way and drawn as a program of its day.

The OSD (§6.8) is not an exception either: the monitor's own on-screen
display, generated inside the monitor and mixed into the picture, the way a
digital monitor of the period put its menu on the glass. It exposes every
tube setting - including the ones a real set kept behind a service door -
and is opened from the case's OSD key or a chord no DOS program would use.

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
  digits, the knobs, the keys and the light from the picture are drawn over the baked
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
compiler. The same noise ages the case: scratches where hands go, scuffs
low down, and soft warm stains over a finer mottle, sized in millimetres so
they are the same stains at every resolution.

All lettering uses compiled-in bitmap fonts: an 8x8 face for the moulded
lettering on the case, Helvetica Bold for its printed legends - POWER, FPS
and MHZ, the mouse lamps' words, and every key's cap - and the VGA 8x16
face for text on the tube. No system fonts anywhere: CoreText, fontconfig and GDI disagree on
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
   MULTIMEDIA sticker under it, centred between the pod and the base. On a display too narrow
   for them, the tube shrinks rather than the speakers vanishing.
4. **The base band** below holds, from the left, the power button and its
   LED, the turbo module, the DXM mark engraved on the centre line, the
   vents and the floppy drive, at real-world sizes in millimetres. The
   sticker and the marks are left off where they do not fit.
5. **The knobs** go under the right speaker when there is room, and on the
   band beside the turbo module when there is not; the MULTIMEDIA sticker
   goes under the left speaker, centred between the pod and the base, when
   they are there. The
   MOUSE key and its lamps go on the left pod under the holes, and **the OSD
   button** opposite them on the right pod; each is left off where its pod is
   too small for it.

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
| 0 | Signal: the source, with the monitor's OSD mixed into it | the signal's size |
| 1 | Phosphor persistence: a decaying accumulator, green longest (P22) | the signal's size, mipmapped |
| 2 | Edge profiles: the light at each of the picture's four edges, weighted by distance from it | 96 x 4 |
| 3 | Ease: the edge light follows the picture with a tenth of a second of inertia | 96 x 4 |
| 4 | Glow field: every point of every edge an emitter, summed over the case | 128 x 96 |
| 5 | Burn-in: a far slower average of the same signal | the signal's size |
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

**The signal is the monitor's input,** and everything after it sees only
that: the PC's picture with the monitor's OSD (§6.8) switched into it, the
way a monitor's OSD generator mixed its display into the video ahead of
the tube. So the OSD is not laid over the finished picture: it persists,
burns in, blooms, lights the case, and takes brightness, contrast, the
beam, the mask and the curvature exactly as the picture does.

The signal's size is the source's, scaled by whole numbers until it is at
least 640x400 - the OSD's raster, and a VGA monitor's lines. Whole numbers,
so each source pixel becomes an exact block and the PC's picture passes
through unresampled; the persistence and burn-in targets take that size.
A fixed-size target would resample every other mode onto its grid and read
it back as its own size, which is what once compressed every picture that
was not 640x400.

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

**The power button.** Pressed with the mouse released, it goes down and
stays down while held; let go of over the button, it switches the machine
off - slide off it first and nothing happens: the same power-down EXIT at the prompt runs, after which the
program ends.

**The knobs.** Brightness and contrast, turned by dragging with the mouse
released to the desktop. Each is drawn over the finished plastic and redrawn
alone when turned.

**The keys.** MOUSE, OSD, MODE, − and + and the power button are momentary: a click sends one
down and it comes back up on its own, down in 60 ms, a moment at the
bottom, up in 130. The cap sinks into its outline, showing a band of the
hole's wall above it; its shadow draws in and its lit slope dims. Like the
knobs, each is drawn over the finished plastic, taking the case's
yellowing and key light where it sits, and a press redraws that key and
any neighbour its square reaches, each at its own depth.

**The turbo module.** A display and three keys beside the power switch:
flat caps lying on the module's plate, the power cap's construction at a
fraction of the size, lettered like the MOUSE and OSD keys.
The display shows the CPU clock in MHz, or with MODE the frames per second
the machine draws; an LED against the printed legend says which. − and +
step the clock through period speeds, each set in DOSBox as a cycle count
(§7). The segments and the mode LEDs cross-fade over a fifth of a second
rather than snapping.

**The OSD.** The monitor's on-screen display, over whatever is showing -
the POST, DOS, a game, SETUP, CATALOG. It is drawn into an RGBA image the
size of a 640x480 picture and mixed into the monitor's signal at the head
of the tube pipeline (§6.4), so from there on it is the picture: it leaves
a phosphor trail as it closes, lights the case, blooms, and takes
brightness and contrast, the scanlines, the mask and the curvature.
A nearly opaque dark blue box with light grey lettering in a doubled 8x8,
across the lower half of the picture, so the upper half stays in view while
it is adjusted. All sixteen tube settings, on three pages: PICTURE
(brightness, contrast, bloom, persistence, scanlines, pixel grid), GEOMETRY
(curvature, jitter, horizontal sync, RGB shift) and TUBE (burn-in, static,
flicker, glow line, chassis glow, ambient light), each a bar and a value
from 0 to 100.

It never takes the mouse. The OSD key on the case opens it and closes it
again, and so does Shift+F1 from anywhere. While it is up it has the arrows
- up and down to choose, left and right to adjust by one of the hundred,
Shift by ten - TAB and Shift+TAB (or PAGE UP and PAGE DOWN) for the pages,
HOME and END for either end, and ESC; every other key goes on to
the machine, so a game does not stop while its picture is adjusted. The
values, and the knobs', persist in `crt.cfg` in the preferences directory.
Under `--deterministic` the file is neither read nor written, so a golden
frame never measures somebody's contrast setting.

**The mouse** is the machine's while it holds it: SDL's relative mode, no
pointer over the glass, motion to whatever is on the tube - DOSBox, SETUP
or CATALOG. Ctrl+F10 releases it to the desktop, as in DOSBox, where it
wears a period arrow and works the knobs and keys. Released, nothing on the tube hears
it: SETUP and CATALOG hide their pointers and light nothing under them, and
whatever the machine held down as the mouse left - a SETUP slider, a DOS
game's fire - is let go, so nothing stays pressed. On a Mac, whose F10 is a
media key, a tap of Command alone does the same.

**The MOUSE key.** On the left speaker pod under the holes, in the manner
of an eighties hi-fi front: CTRL+F10 printed over a MOUSE key - a grey
keycap with a firm outline and wide angled slopes in to its face - and from
under the key a printed line that drops and branches down to two LEDs side
by side, HOST and DXM under them. The lit LED says who holds the mouse,
cross-fading like the mode LEDs and dark with the machine. The key gives
the mouse to the machine; that is the only way a press can move it, since
while the machine holds it there is no pointer to press with, and CTRL+F10
takes it back.

**The OSD button.** A momentary button on the right speaker pod under the
holes, opposite the MOUSE key, a raised key the MOUSE key's size with OSD
printed on it. It opens the OSD, and a second press closes it.

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

## 8. Drives, and the catalogue

### 8.1 Drives

The machine owns a C: drive: a folder in its preferences directory, created
on first run, mounted by DOSBox as C:. DOS software is put there as it sat
on a hard disk. `--dosbox DIR` mounts another folder instead. ROM files for
an MT-32 or an SC-55 go in the root, where DOSBox Pure finds them and puts
the device on the MPU-401.

Every catalogue (§8.2) is a drive, on the letter it names, mounted as the
machine boots. A catalogue on a letter other than C: is the folder the
machine keeps for it, `catalogues/<id>` in the preferences, so what is
installed from it lives there and nothing ever moves; the drive's volume
label is the catalogue's id. The freeware catalogue is on C:, which is to
say it installs into the C: folder above; the shareware one is on D:. Which
catalogue is on which letter is the catalogue's own say for now; when there
are catalogues to choose between, SETUP gets a DRIVES section - a letter,
and what is on it, a catalogue or a local folder - and since drives are
hardware, it takes effect at the next power-on through SAVE & REBOOT (§6.9).

### 8.2 The catalogue files

A catalogue is a `.cat` file, JSON, shipped in the `catalogues` folder beside
the program for now and read from disk; fetching one from a URL later
changes where it comes from and nothing about it. There is no list of them:
the machine takes every `.cat` in the folder, so adding a catalogue is adding
a file.

**A `.cat` file** says where it goes, then what it holds: `format`; an `id`
(eight characters at most: it names the host folder and is the drive's
volume label); a `name`; an `origin`, `bundled` or `community`, which the
bundled ones carry too, so that whoever writes a catalogue sees the choice;
the `drive` it is on, a letter from C to Z; then `about`, `updated`, and
`titles`. The tabs are in the order of the drives. A catalogue without an
id, a drive or an origin is not read, and one wanting a letter or an id
another already has is left out, both with a line in the log - the files
being taken in name order, so which one wins is the same on every
machine. A title has, required: `id`, `name`, `creator`, `year`,
`category`, `multiplayer` and `network` (booleans), `run`, and `download`
(`url`, `size`, `sha256`). Optional: `publisher`, `version`, `genre`,
`description`, `video` (CGA, EGA, VGA, SVGA), `sound` and `controls`
(lists), `setup` (the title's own configuration program, whatever it is
called, `SETUP.EXE` or `SOUND.BAT`), `archive` (for a download that is really an
installer, the path inside it of the archive that holds the title, a zip or
a zip that extracts itself, unpacked in its place) and `artwork` (`url`,
`sha256`).
`id` and `category` are DOS directory names, eight characters or fewer, and
`category` is one of a fixed list, plural because it is a folder:
`GAMES`, `TOOLS`, `EDUCATION`, `MUSIC`, `DEMOS`, `MISC`. `name` is at most
64 characters and `description` about 400, since the screen has to fit
them. A catalogue is validated as it is read, and a bad title is dropped
with a line in the log rather than taking the catalogue with it.

Nothing in a title says where it goes: it goes to `\<CATEGORY>\<ID>` on
whatever drive its catalogue is mounted on. A title is *installed* when
`\<CATEGORY>\<ID>\<run>` exists there, and only then.

Catalogues are licences, not subjects: `freeware` first, `shareware` next.
What is in DXM's own repository is what DXM may distribute; a catalogue of
software that is nobody's to give away is not one of them (§12.4).

### 8.3 CATALOG

`CATALOG` at the DOS prompt is a program the fork adds, like `SETUP`, and
the screen it opens is the machine's, drawn on the SETUP canvas with the
same font and pointer (§6.9) but as an application rather than a BIOS
screen, and a plain one: a dark ground with no chrome on it, wells a shade
darker with a hairline round them, light grey text, white for what
matters, gold for the keys and the one chosen thing. The catalogues as
tabs across the top, the chosen one underlined, with the count - titles
showing, installed, the drive - on the same line to the right, where an
install's progress and its outcome also appear. On the left a row of
filters, each cycled by a click and gold while it narrows the list - the
type (all, games, education, tools, music), players (all, single, multi)
and network (all, yes, no) - and a [+] that opens a panel with more: year,
video, sound, controls, and whether the title is on the drive. F4, F5 and
F6 cycle the three (backwards with Shift) and F7 opens and closes the
panel, so the filters are there from the keyboard too. In the panel the
arrows move a cursor down its rows and Reset, ENTER cycles the filter under
it (or resets), left and right cycle it either way, and ESC closes it. Under them
a Find field over the list of titles - name, year, a mark for one that is
on the drive - with a scrollbar; typing goes to the field and the list
narrows as you type, Backspace edits it, ESC clears it, and only then
leaves. On the right the chosen title's picture, its name, maker, year and
genre, its particulars as a label and a value a line - players, video,
sound, controls, in words rather than the catalogue's codes, each on one
line, a value too long for it running past like a marquee after a moment
to read its start - and its description. Everything under the picture scrolls as one when it is longer
than the space: the wheel over that side, or Shift with the arrows and
Page Up and Down, with a thin scrollbar beside it; a new title starts at
the top. Along the foot the keys and
what they do, as SETUP has them: ENTER installs, or runs once the title is
on the drive (so does a double-click); F2 its setup where it has one; F3 a
prompt in its directory; left and right the catalogues; ESC. Letters are the
search's, which is why the actions are on function keys; the hints are
also what the mouse presses. Under the list, the count, and the download's
progress while there is one. It comes up the way SETUP does, with a
moment of loading screen, and then arrives in horizontal bands slid in
from alternate sides - the same arrival, without the loading, when it
comes back from an errand. Leaving, it fades to black on the palette, and
DOS waits at `CATALOG` until it has, so the prompt never shows under it.

**Artwork** is shown before a title is installed, fetched when the title is
first selected and kept at `artwork/<catalogue>/<CATEGORY>/<ID>-<hash>.<ext>`
in the preferences directory, outside DOS's view - named for the picture's
hash as well as the title, so a catalogue that changes a picture has it
fetched afresh. That cache is looked in first, always, and is what an
offline machine shows; a title without a picture, or whose picture has not
arrived, shows the machine's own mark in its place. Whatever
shape the picture comes in is fitted to the well and to the palette entries
the interface leaves it, as the banners are.

**RUN, SETUP and PROMPT are excursions, not exits.** The screen goes away,
DOS has the tube, and when the excursion ends the catalogue is back exactly
as it was: the same tab, the same title, the same scroll. The program in the
fork does the running, as a 1993 program did: it notes the current drive
and directory, changes to the title's, executes `run` (or `setup`) and
waits for it, changes back, and asks the machine to show the screen again.
PROMPT spawns a nested `COMMAND` in the title's directory, after the line
every such program printed - *Type EXIT to return to CATALOG* - and `EXIT`
is the way back. A title that hangs DOS hangs the catalogue with it, since
it runs inside it; that is period-correct too, and the reboot is the fix.
ESC is the only exit, to the prompt `CATALOG` was typed at.

### 8.4 Installing

INSTALL downloads the archive, one at a time, with a progress bar and
nothing else to do until it is done. The download is refused past a size
cap, and discarded if its sha256 is not the catalogue's - said plainly, no
override. The zip is opened with `..` and absolute paths refused, and so is
a zip that extracts itself, which is a zip with a program in front of it;
for a download that is really an installer, the title's `archive` inside it
is opened the same way in its place. The folder that holds `run` is the
title, wherever the packer put it, and that folder's contents go to
`\<CATEGORY>\<ID>`. The download and everything unpacked from it are then
deleted.
A directory already there, whatever is in it, means INSTALL refuses and
says so. DOSBox's cached listing of the drive is refreshed afterwards, so
`DIR` sees the title without a `RESCAN`. There is no uninstall in 2.0:
removing a title is deleting its directory, from DOS or the desktop.

### 8.5 Not in 2.0

The community list of catalogues and the machinery around it, importers
(an eXoDOS folder, GOG installers), fetching a single title out of a
torrent, and uninstall. The formats are written so none of these change
them: identity is the hash, and where the bytes come from is one way of
resolving it.

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
- **The catalogue** (§8), in this order: the reader and its validator,
  DRIVES in SETUP, the screen against the bundled `.cat`, then download
  and install.
- **A bridge between the machine and DOS** for anything that needs one,
  through files on C: before any change to the core.

## 11. Non-goals, and the dev flags

- Windowed mode, except the `--windowed` dev flag.
- A settings screen outside the fiction. SETUP and the OSD (§2.1) are the
  machine's and the monitor's own.
- Save states, netplay, gamepads, and more than one DOS at once.
- Shipping any DOS software or data. The catalogue (§8) downloads what its
  publisher gives away; the machine carries none of it.
- Emulating anything itself. The machine draws, sounds and holds the DOS;
  DOSBox emulates.

The dev flags are the exceptions to the appliance: `--windowed`, `--size`,
`--deterministic`, `--shot` and `--frames`, `--type`, `--ambient`,
`--dump-audio`, `--dosbox` and `--dosbox-core`, `--setup` and `--catalog`.
F5 and F6 during the POST
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

### 12.4 What the catalogue may list

A catalogue in DXM's repository is DXM distributing what it lists, one
step removed, and the step does not change whose name is on it. So the
bundled catalogues hold software its publisher gives away - freeware, and
shareware, whose licences permit passing it on - mirrored where the
original download is gone or unreliable, and nothing else. A catalogue of
software that is copyrighted and merely unenforced is not for this
repository under any name, and not for a machine that ships a pointer to
it either: what the law turns on is knowledge, and a built-in pointer is
knowledge. When there is a list of community catalogues (§8.5), it is
theirs, submitted by their maintainers and labelled so; DXM ships no entry
in it, acts on notice title by title through a block list keyed by hash,
and delists a catalogue that keeps drawing notices.

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
