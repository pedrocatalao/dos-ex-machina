# What DOS ex Machina is built on

DOS ex Machina itself is free software: you can redistribute it and modify
it under the terms of the GNU General Public License, version 2 or, at your
option, any later version. See [LICENSE](LICENSE). The code on the
[`legacy`][legacy] branch, the earlier incarnation that ran natively ported
games, was MIT licensed and remains so.

- **DOSBox Pure** is GPL-2.0, by Bernhard Schelling and the DOSBox team.
  Every build carries it, built from [the fork][fork], with its licence
  beside it as `LICENSE-dosbox-pure.txt`; its source is the submodule in
  `external/dosbox-pure`, at the commit this repository records.
- **FreeDOS EDIT** 0.9b travels in the DOSBox core, on Z:, under the
  GPL-2.0; its source, with that of the FreeDOS D-Flat+ library it is built
  on, is in the fork beside it, in `dxm/edit/`.
- **`src/dosbox/libretro.h`** is the libretro API header, vendored as it
  ships, under the MIT licence.
- **The VGA font** is FreeBSD's `cp437-8x16`, under the BSD licence.
- **The interface font** is X11's Adobe Helvetica bitmap, copyright
  1984-1989, 1994 Adobe Systems and 1988, 1994 Digital Equipment
  Corporation, redistributable under the notice in the `.bdf` files.
- **`src/third_party/stb_image.h`** is Sean Barrett's image loader, vendored
  as it ships, in the public domain (MIT at your option).
- **SDL3** is linked, not vendored, under the zlib licence; so are
  **libcurl** (the curl licence, MIT-like) and **zlib** (the zlib licence),
  for the catalogue's downloads.

**No software or data for DOS is in this repository or in its builds.** The
catalogue downloads what its authors give away, from where they put it, when
you ask for it.

[fork]: https://github.com/pedrocatalao/dosbox-pure/tree/dosexmachina
[legacy]: https://github.com/pedrocatalao/dos-ex-machina/tree/legacy
