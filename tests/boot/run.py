#!/usr/bin/env python3
"""Boot smoke test for DOS ex Machina.

    run.py <dxm binary>

Boots the machine the whole way: the POST, DOSBox taking the tube, a DOS
command, and EXIT powering the machine off.  It types MEM and then EXIT at
the prompt and reads the log the machine writes to stderr, which must show
each step in order, and the program must end on its own with status 0.

This is the half of the machine the golden frames cannot see: once DOSBox
has the tube its picture runs on its own clock, so what is checked here is
that the core loaded, the handover happened, keys reached the DOS and a
program ran, and the machine shut down cleanly.  Under the sanitizers it is
also the run that exercises the bridge with a real core behind it.

No dependencies beyond Python 3.
"""
import subprocess
import sys

# what the log must show, in this order
STEPS = [
    ("the core booted", "dosbox: booted"),
    ("DOSBox took the tube", "dosbox: has the tube"),
    ("MEM ran", "Program: MEM"),
    ("the machine powered off", "power off"),
    ("DOS said EXIT", "DOS said EXIT"),
]


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    cmd = [sys.argv[1], "--windowed", "--deterministic", "--size", "800x600",
           "--type", "MEM;EXIT"]
    try:
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                           timeout=120)
    except subprocess.TimeoutExpired as e:
        log = (e.stderr or b"").decode(errors="replace")
        print(f"FAIL: the machine did not power off within 120 s\n{log}")
        sys.exit(1)
    log = r.stderr.decode(errors="replace")
    pos, failed = 0, False
    for what, text in STEPS:
        i = log.find(text, pos)
        if i < 0:
            print(f"FAIL: {what} - no \"{text}\" in the log after the step before")
            failed = True
            break
        print(f"ok   {what}")
        pos = i + len(text)
    if r.returncode != 0:
        print(f"FAIL: dxm exited {r.returncode}")
        failed = True
    if failed:
        print(log)
        sys.exit(1)


if __name__ == "__main__":
    main()
