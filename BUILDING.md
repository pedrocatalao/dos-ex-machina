# Building DOS ex Machina

## Requirements

SDL3, a C and a C++ compiler, GNU make, libcurl and zlib. Python 3 for the
golden and boot tests.

To run, an **OpenGL 3.3 core** context. That rules out most virtual
machines: virgl on an Apple Silicon host offers only a 2.1 compatibility
profile, and Windows without a GPU driver gives Microsoft's software GL 1.1.
On Linux you can force Mesa's software rasteriser with
`LIBGL_ALWAYS_SOFTWARE=1`, which works but is far too slow to be pleasant.
If DXM cannot get what it needs it says so on screen, naming the driver and
the functions that were missing.

## Build

```bash
git clone --recursive https://github.com/pedrocatalao/dos-ex-machina
cmake -S dos-ex-machina -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/dxm
```

The DOSBox Pure core is the submodule at `external/dosbox-pure`, pinned to a
commit of [the fork][fork]; CMake builds it with its own Makefile and puts
it beside `dxm`, where the machine looks for it. In a clone made without
`--recursive`, `git submodule update --init` fetches it. `-DDXM_CORE=OFF`
skips the core, for a machine run with `--dosbox-core PATH`.

SDL3 is not yet in Ubuntu's archive, so on Linux it has to be built from
source. The tube's shaders are the GLSL files under `shaders/`, baked into
the binary at build time; nothing is read from disk at run time but the
core.

On Apple Silicon the build signs `dxm` ad hoc with the JIT entitlement in
`packaging/macos/`, which DOSBox's dynamic core needs.

## Tests

`ctest --test-dir build` runs them all.

- `-L unit` needs no display.
- `-L golden` draws the machine under `--deterministic` and compares the end
  of the POST pixel for pixel with the reference frames in
  `tests/golden/references/`, whose [README](tests/golden/references/README.md)
  says when and how they change.
- `-L boot` boots the whole machine, types a DOS command and `EXIT`, and
  checks the log.

The golden and boot tests need a display and Python 3.

`-DDXM_SANITIZE=ON` builds with AddressSanitizer and
UndefinedBehaviorSanitizer; CI runs the tests that way, headless, against a
software GL driver.

## Formatting and generated files

The C is formatted with the project's `.clang-format`: `tools/format.sh`
applies it, and CI checks it with the same pinned release of clang-format.

The data baked under `src/gen/` is machine output from the sources in
`assets/`; `tools/regen.sh` rebuilds it and `tools/regen.sh --check` is what
CI runs. [src/gen/README.md](src/gen/README.md) lists each file.

The website is the `docs/` folder, served by GitHub Pages. Its User's Guide
page, `docs/manual.html`, is made from [MANUAL.md](MANUAL.md) by
`tools/mksite.py` and is never edited by hand; `tools/mksite.py --check`
fails if the two have drifted apart.

[fork]: https://github.com/pedrocatalao/dosbox-pure/tree/dosexmachina
