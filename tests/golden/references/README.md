# Golden reference frames

One directory per platform (`macos/`, `linux/`, `windows/`), each holding
the frames `run.py` captured there. A frame is only comparable with a frame
from the same GPU and driver: the tube pipeline is float maths on whatever
hardware is present, and two drivers do not round alike.

The macOS references are from the development machine (Apple GPU). A CI
runner for another platform blesses its own set with

    python3 tests/golden/run.py build/dxm --update

after someone has looked at the frames it produced.

## When references change

- Any change to the chassis, the tube pipeline, the font, the DOS text or
  the boot sequence changes frames. That is the point: the test exists so
  such a change is a decision, not an accident. Look at the diff image the
  failed run leaves in the build directory, and if the new frame is the
  intended one, `--update` and commit the new reference with the change.
- A version bump changes the BIOS banner and so the prompt frames. Update
  the references in the same commit as `project(... VERSION ...)`.
- A refactor must not change them. A pixel-exact pass on every case is
  what "behaviour-neutral" means for this code base.
