# Golden reference frames

One directory per **renderer**, each holding the frames `run.py` captured
with it. A frame is only comparable with a frame from the same GPU and
driver: the tube pipeline is float maths on whatever hardware is present,
and two drivers do not round alike. The operating system is beside the
point; the driver is not.

| Set | Drawn by | Where |
|---|---|---|
| `apple-gpu/` | an Apple GPU | the machine the project is developed on |
| `llvmpipe/` | Mesa's software renderer | the Sanitizers workflow, headless |

`run.py --refs NAME` picks the set; without it the script guesses from the
platform, which is right on the two machines that have a set here and
wrong anywhere else. A third machine passes its own `--refs`.

`apple-gpu` is updated from a developer's machine:

    python3 tests/golden/run.py build/dxm --update

`llvmpipe` is drawn on a runner nobody has locally, so it is updated from
there: run the Sanitizers workflow by hand with **update_references**
ticked and it re-captures the frames and commits them. Look at the failing
run's diff images first — that switch blesses whatever the machine now
draws.

Nothing updates a set on its own. Every push and every pull request
compares, and a set that is missing fails the job rather than quietly
capturing one: a run that passes while checking nothing is worse than a
run that fails.

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

## The floppy LED

In normal running the activity LED follows the drive sound's envelope on
the audio thread, in real time. Under `--deterministic` it follows the
machine clock instead: on for exactly the seconds the drive was asked to
run, then off. That is what makes a frame taken while the drive runs
reproducible; it also means such a frame shows a lamp that is simply on,
without the envelope's flicker.
