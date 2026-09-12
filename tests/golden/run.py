#!/usr/bin/env python3
"""Golden-frame test for DOS ex Machina.

    run.py <dxm binary> [--update] [--refs NAME] [--out DIR]
                        [--tolerance N] [--only NAME]

Runs the machine under --deterministic for each case below, captures the
frame the case names, and compares it pixel for pixel with the reference
PNG committed beside this script.  Any difference fails the case and leaves
the captured frame and a difference image in --out for inspection.

--update replaces the references with what the binary draws now.  Do that
only after looking at the frames: a golden test is only as good as the
frame that was blessed.

A reference set belongs to the renderer that drew it, not to an operating
system: two GL drivers do not round alike.  --refs names the set, and
defaults to a guess from the platform.  The sets in git are `apple-gpu`,
from the machine the project is developed on, and `llvmpipe`, from Mesa's
software renderer in CI (see references/README.md).

No dependencies beyond Python 3.  PNGs are written with zlib from the
standard library.
"""
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

# name, window size, frame to capture.  Frame numbers are in sixtieths of
# a second of machine time.  Frame 290 is the end of the POST: the memory
# count done, the drives detected, and the SETUP prompt and the clock along
# the bottom, a few frames before the DOS takes the tube.  The BIOS's
# second screen is not here: DOSBox draws that one (src/dosbox, and
# dosbox_pure_dxm.h in the fork).  Everything up to that
# handover is the machine's own drawing on the fixed clock, so it is the
# same picture on every run; after it the picture is DOSBox's, which runs
# on its own clock and cannot be compared frame for frame (tests/boot
# checks that part instead).
CASES = [
    ("post-1280x800", "1280x800", 290),
    ("post-1720x720", "1720x720", 290),
]


def default_refs():
    """A guess at which renderer this machine draws with.  Right for the
    machines that have a set in git; anywhere else, pass --refs."""
    import platform
    return {"Darwin": "apple-gpu"}.get(platform.system(), platform.system().lower())


def read_bmp(path):
    d = open(path, "rb").read()
    off = struct.unpack("<I", d[10:14])[0]
    w = struct.unpack("<i", d[18:22])[0]
    h = struct.unpack("<i", d[22:26])[0]
    bpp = struct.unpack("<H", d[28:30])[0]
    if bpp != 24:
        raise SystemExit(f"{path}: expected 24-bit BMP, got {bpp}")
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(h):
        r = d[off + (h - 1 - y) * stride: off + (h - 1 - y) * stride + w * 3]
        # BMP is BGR
        rows.append(bytes(b for px in range(w) for b in (r[px*3+2], r[px*3+1], r[px*3])))
    return w, h, rows


def write_png(path, w, h, rows):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + r for r in rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def read_png(path):
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos+4])[0]
        tag = d[pos+4:pos+8]
        body = d[pos+8:pos+8+ln]
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if depth != 8 or ctype != 2:
                raise SystemExit(f"{path}: expected 8-bit RGB (as run.py writes)")
        elif tag == b"IDAT":
            idat += body
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * 3
    rows = []
    prev = bytes(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1: (y + 1) * (stride + 1)])
        if f == 1:
            for i in range(3, stride):
                line[i] = (line[i] + line[i - 3]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f != 0:
            raise SystemExit(f"{path}: unsupported PNG filter {f}")
        rows.append(bytes(line))
        prev = rows[-1]
    return w, h, rows


def compare(a_rows, b_rows, w, h, tolerance):
    """Returns (differing pixels, max channel delta, diff rows)."""
    diff_rows, count, maxd = [], 0, 0
    for y in range(h):
        ra, rb = a_rows[y], b_rows[y]
        if ra == rb:
            diff_rows.append(bytes(w * 3))
            continue
        line = bytearray(w * 3)
        for x in range(w):
            i = x * 3
            d = max(abs(ra[i] - rb[i]), abs(ra[i+1] - rb[i+1]), abs(ra[i+2] - rb[i+2]))
            if d > tolerance:
                count += 1
                if d > maxd:
                    maxd = d
                v = min(255, 64 + d * 3)
                line[i], line[i+1], line[i+2] = v, 0, 0
        diff_rows.append(bytes(line))
    return count, maxd, diff_rows


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    binary = args.pop(0)
    update, only, tolerance, refs = False, None, 0, default_refs()
    out = os.path.join(os.getcwd(), "golden")
    while args:
        a = args.pop(0)
        if a == "--update":
            update = True
        elif a == "--out":
            out = args.pop(0)
        elif a == "--tolerance":
            tolerance = int(args.pop(0))
        elif a == "--only":
            only = args.pop(0)
        elif a == "--refs":
            refs = args.pop(0)
        else:
            raise SystemExit(f"unknown argument {a}\n{__doc__}")
    os.makedirs(out, exist_ok=True)
    refdir = os.path.join(HERE, "references", refs)
    os.makedirs(refdir, exist_ok=True)

    failed = 0
    for name, size, frame in CASES:
        if only and name != only:
            continue
        bmp = os.path.join(out, name + ".bmp")
        cmd = [binary, "--windowed", "--deterministic", "--size", size,
               "--shot", bmp, "--frames", str(frame)]
        if os.path.exists(bmp):
            os.remove(bmp)
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                           timeout=120)
        if r.returncode != 0 or not os.path.exists(bmp):
            print(f"FAIL {name}: dxm exited {r.returncode}\n{r.stderr.decode(errors='replace')}")
            failed += 1
            continue
        w, h, rows = read_bmp(bmp)
        os.remove(bmp)
        ref = os.path.join(refdir, name + ".png")
        if update:
            write_png(ref, w, h, rows)
            print(f"updated {os.path.relpath(ref)} ({w}x{h})")
            continue
        if not os.path.exists(ref):
            print(f"FAIL {name}: no reference at {os.path.relpath(ref)} (run with --update)")
            write_png(os.path.join(out, name + ".actual.png"), w, h, rows)
            failed += 1
            continue
        rw, rh, rrows = read_png(ref)
        if (rw, rh) != (w, h):
            print(f"FAIL {name}: size {w}x{h}, reference {rw}x{rh}")
            failed += 1
            continue
        count, maxd, drows = compare(rows, rrows, w, h, tolerance)
        if count:
            write_png(os.path.join(out, name + ".actual.png"), w, h, rows)
            write_png(os.path.join(out, name + ".diff.png"), w, h, drows)
            print(f"FAIL {name}: {count} pixels differ (max channel delta {maxd}); "
                  f"see {os.path.join(out, name)}.diff.png")
            failed += 1
        else:
            print(f"ok   {name} ({w}x{h}, frame {frame})")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
