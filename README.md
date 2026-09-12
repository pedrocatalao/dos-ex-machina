<p align="center">
  <img src="docs/logo-readme.png" alt="DOS ex Machina" width="720">
</p>

A 1993 beige-box PC on your screen, case and CRT and all, that boots a real
DOS. The machine powers on, runs its POST, and hands the tube to DOSBox at
the prompt. From there it is a DOS PC: whatever you put on its C: drive runs
behind the same glass, in the same phosphor, with the case lit by what is on
screen.

> **2.0.** macOS, Windows and Linux, on x86_64 and arm64. The DOS is
> [DOSBox Pure][dbp], built from a fork and shipped inside every release.
> The downloads are not code-signed, so each platform shows its first-run
> warning once; see Installing for the way through it.

Nothing here is a photograph. The machine is drawn procedurally at your
display's resolution, from signed-distance geometry and a lighting model: the
moulding partings, the speaker pods, the louvres, the vent cuts and thirty
years of wear are all solved, not painted. The tube is a real pipeline:
phosphor persistence, barrel curvature, footprint-integrated scanlines, an
aperture-grille mask, bloom, and light from the picture spilling onto the
plastic around it.

[dbp]: https://github.com/schellingb/dosbox-pure

## What it is, and what it was

DOSBox runs DOS. DOS ex Machina is the machine it runs in: the case, the
tube, the drive that spins when a program loads, the power switch. DOSBox's
picture goes through the same CRT pipeline the machine draws its own POST
with, and the handover between the two is invisible: the BIOS clears its
first screen, and the second one, the system configuration, is drawn by
DOSBox itself, with DOS's prompt coming up under it in the same VGA font.

Version 1 ran natively ported DOS games instead, loaded as modules and
installed from a catalogue, with a simulated DOS prompt and a navigator in
front of them. That code, its porting contract and its documents live on the
[`legacy`][legacy] branch, and the [1.0 release][1.0] is tagged.

[legacy]: https://github.com/pedrocatalao/dos-ex-machina/tree/legacy
[1.0]: https://github.com/pedrocatalao/dos-ex-machina/releases/tag/1.0

## Installing

Every release is self-contained: unpack it and run it. The DOSBox core
travels inside it.

| Platform | Download |
|---|---|
| **macOS** | [universal][dl-mac], Intel and Apple silicon in one bundle |
| **Windows** | [x86_64][dl-win-x64] · [arm64][dl-win-arm] |
| **Linux** | [x86_64][dl-lin-x64] · [arm64][dl-lin-arm] |

Those links always point at the newest release; [the releases
page](https://github.com/pedrocatalao/dos-ex-machina/releases) has the notes
and the older ones.

[dl-mac]: https://github.com/pedrocatalao/dos-ex-machina/releases/latest/download/dxm-macos-universal.zip
[dl-win-x64]: https://github.com/pedrocatalao/dos-ex-machina/releases/latest/download/dxm-windows-x86_64.zip
[dl-win-arm]: https://github.com/pedrocatalao/dos-ex-machina/releases/latest/download/dxm-windows-arm64.zip
[dl-lin-x64]: https://github.com/pedrocatalao/dos-ex-machina/releases/latest/download/dxm-linux-x86_64.tar.gz
[dl-lin-arm]: https://github.com/pedrocatalao/dos-ex-machina/releases/latest/download/dxm-linux-arm64.tar.gz

On macOS the release is a `DOS ex Machina.app` bundle. It is ad-hoc signed
rather than notarised, so the first launch needs **right-click → Open**;
double-clicking will refuse it. If it still balks, System Settings → Privacy
and Security → General has an **Open Anyway** button.

On Windows, unzip and run `dxm.exe`. Everything it needs is in the same
folder and nothing has to be installed. The executable is unsigned, so
SmartScreen shows *"Windows protected your PC"* the first time: **More
info → Run anyway**.

On Linux the tarball runs from wherever you unpack it: `./dxm`. If you want
it in your applications menu, `./install.sh` puts it under `~/.local`,
writes a desktop entry and installs the icon. It needs no root, touches
nothing system-wide, does not modify your shell configuration or `PATH`, and
prints exactly what to delete to undo it.

## Your C: drive

The machine has its own C: drive, a folder in its preferences directory,
created the first time it runs:

| Platform | C: is |
|---|---|
| macOS | `~/Library/Application Support/DOSexMachina/dxm/C` |
| Windows | `%APPDATA%\DOSexMachina\dxm\C` |
| Linux | `~/.local/share/DOSexMachina/dxm/C` |

Put your DOS software in it, as folders, the way it sat on a hard disk. The
machine writes one file there, `DOSBOX.BAT`, which DOSBox runs at boot to
print the greeting under the BIOS's second screen. Replace it with your own
and the machine leaves it alone.

`--dosbox DIR` mounts another folder as C: instead, and so does the
`DXM_DOSBOX` environment variable.

No software comes with it. Plenty of DOS software was released as freeware
by its authors, and their own sites are the place to get it.

**MIDI.** For a Roland MT-32 or a Sound Canvas SC-55, put the ROM files in
the root of C:: `MT32_CONTROL.ROM` and `MT32_PCM.ROM` for the MT-32, the
`.BIN` set for the SC-55. DOSBox finds them itself and puts the device on the
MPU-401 at port 330h; set the program's music to that.

## Using it

Fullscreen is real fullscreen: the case fills the display edge to edge with
no background around it. Extra width becomes more machine, never
letterboxing.

The machine powers on, degausses, warms up and runs its POST. By the time
the memory count is done, DOSBox has booted behind it; the screen clears,
the system configuration comes up, the drive runs, and the DOS prompt
arrives under it. `EXIT` powers the machine down properly: the raster
collapses, the fans spin down, and the room goes dark.

**The mouse** belongs to the machine: no pointer over the glass, and a DOS
program that uses a mouse gets it. **Ctrl+F10** gives it back to your
operating system, where it wears a period arrow and can work the controls
on the case, and takes it back again. On a Mac, a tap of **Command** on its
own does the same.

**The two knobs** under the right speaker are brightness and contrast: grab
one and drag up or down.

**The turbo display** beside the power button shows the CPU clock in MHz.
Its **−** and **+** buttons step it through 33, 40, 50, 66, 80 and 100 MHz,
which set DOSBox's speed from a 386DX to a 486DX4; 66 is the default. A game
that paces itself by the clock plays the same at any of them, and anything
that runs flat out shows the difference. **MODE** switches the display to the
frames per second the machine is drawing.

**Shift+F1** opens a panel over the tube with the rest of the CRT: bloom,
burn-in, static, jitter, glow line, ambient light, flicker, h-sync, RGB
shift, chassis glow, persistence, scanlines, pixel grid, curvature,
brightness and contrast. Everything saves to `crt.cfg` in the preferences
directory.

Every run writes `dxm.log` to the preferences directory, one folder up from
C:, with a timestamp for each startup step, the GL driver in use and what
DOSBox reports. If something goes wrong, that file is the bug report.

Dev flags: `--windowed`, `--size WxH`, `--type "CMD;CMD"` (typed at the DOS
prompt, one a second), `--shot out.bmp --frames N`, `--deterministic` (a
fixed 60 Hz clock and the shipped CRT defaults, so a given frame is the same
picture on every run), `--ambient N`, `--dump-audio FILE`,
`--dosbox DIR`, `--dosbox-core PATH`.

## Requirements

An **OpenGL 3.3 core** context. That rules out most virtual machines: virgl
on an Apple Silicon host offers only a 2.1 compatibility profile, and Windows
without a GPU driver gives Microsoft's software GL 1.1. On Linux you can
force Mesa's software rasteriser with `LIBGL_ALWAYS_SOFTWARE=1`, which works
but is far too slow to be pleasant. If DXM cannot get what it needs it says
so on screen, naming the driver and the functions that were missing.

## Build

```bash
git clone --recursive https://github.com/pedrocatalao/dos-ex-machina
cmake -S dos-ex-machina -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/dxm
```

It needs SDL3, a C and a C++ compiler, and GNU make. The DOSBox Pure core is
the submodule at `external/dosbox-pure`, pinned to a commit of [the
fork][fork]; CMake builds it with its own Makefile and puts it beside `dxm`,
where the machine looks for it. In a clone made without `--recursive`,
`git submodule update --init` fetches it. `-DDXM_CORE=OFF` skips the core,
for a machine run with `--dosbox-core PATH`.

SDL3 is not yet in Ubuntu's archive, so on Linux it has to be built from
source or taken from the release tarball, which carries it. The tube's
shaders are the GLSL files under `shaders/`, baked into the binary at build
time; nothing is read from disk at run time but the core.

`ctest --test-dir build` runs the tests. `-L unit` needs no display. `-L
golden` draws the machine under `--deterministic` and compares the end of the
POST pixel for pixel with the reference frames in
`tests/golden/references/`, whose README says when and how they change.
`-L boot` boots the whole machine, types a DOS command and `EXIT`, and checks
the log. The golden and boot tests need a display and Python 3.

`-DDXM_SANITIZE=ON` builds with AddressSanitizer and
UndefinedBehaviorSanitizer; CI runs the tests that way, headless, against a
software GL driver. The C is formatted with the project's `.clang-format`:
`tools/format.sh` applies it, and CI checks it with the same pinned release
of clang-format.

[fork]: https://github.com/pedrocatalao/dosbox-pure/tree/dosexmachina

## Known gaps

- **Unsigned.** Code-signing certificates carry a yearly fee to each
  platform's authority, so macOS and Windows each show a first-run warning;
  see Installing for the way through it.
- **Apple Silicon runs DOSBox's interpreter.** The dynamic core needs a
  small patch in the fork before macOS on arm64 lets it generate code. Every
  other platform uses it. The interpreter is fast enough for the DOS era,
  but a demanding late-DOS game may want the patch.
- **The drive LED is a guess.** The floppy runs when a program starts or
  ends, judged by the picture changing mode, not by the emulated drive.
- **One C: drive.** CD-ROM images and floppy images have to be mounted by
  hand from DOS for now.

## Licence

DOS ex Machina is free software: you can redistribute it and modify it under
the terms of the GNU General Public License, version 2 or, at your option,
any later version. See [LICENSE](LICENSE). Releases up to and including 1.0
were MIT licensed, and remain so.

- **DOSBox Pure** is GPL-2.0, by Bernhard Schelling and the DOSBox team.
  Every release carries it, built from [the fork][fork], with its licence
  beside it as `LICENSE-dosbox-pure.txt`; its source is the submodule in
  `external/dosbox-pure`.
- **`src/dosbox/libretro.h`** is the libretro API header, vendored as it
  ships, under the MIT licence.
- **The VGA font** is FreeBSD's `cp437-8x16`, under the BSD licence.
- **SDL3** is linked, not vendored, under the zlib licence.
- **No software or data for DOS is in this repository or in its releases.**

## Design notes

[ARCHITECTURE.md](ARCHITECTURE.md) is the map of the code: what is where,
what each part owns, how a frame is made and how DOSBox fits in.

[SPEC.md](SPEC.md) covers the machine: the principles, how the layout is
solved from the display, the tube pipeline pass by pass, the light on the
case, and what the machine asks of DOSBox.
