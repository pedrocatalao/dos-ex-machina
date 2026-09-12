#!/usr/bin/env python3
"""mkbanners.py <out.c> <out.h> <in.png>...
Bake the SETUP banners: 640x240 artwork shown one pixel for one, faded to
black over its lower half, lifted a little against the tube's own gamma, and
quantised to the 240 palette entries the machine leaves to it - 0..15 belong
to the interface.

The screen is 640 across, so the artwork is drawn at 1:1 or at 2:1 and
nothing in between: at any other scale some of its pixels would be one
screen pixel across and their neighbours two.

The fade is baked rather than blended at run time because the canvas is an
indexed one: indices do not blend, and quantising the faded image spends the
palette on the shades that are actually on screen.

Median cut, on the histogram rather than the pixels, so it is quick and the
same every time."""
import sys, zlib, struct

SRC_W, SRC_H = 640, 240       # what the artwork is drawn at
W, H = 640, 240               # what the machine shows, one for one
FADE_FROM = 0.45              # of the banner's height: full brightness above
LIFT = 1.45                   # against the tube's gamma, which pulls it down
UI_COLOURS = 16               # 0..15 are the interface's
PALETTE = 256 - UI_COLOURS    # what is left for the artwork


def read_png(path):
    """8-bit non-interlaced PNG -> (w, h, [(r,g,b), ...])."""
    d = open(path, 'rb').read()
    pos, idat, w, h, bd, ct, inter = 8, b'', 0, 0, 0, 0, 0
    while pos < len(d):
        ln, = struct.unpack('>I', d[pos:pos + 4])
        typ = d[pos + 4:pos + 8]
        ch = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, bd, ct, _, _, inter = struct.unpack('>IIBBBBB', ch)
        elif typ == b'IDAT':
            idat += ch
        pos += 12 + ln
    assert bd == 8 and inter == 0, f"{path}: expect 8-bit non-interlaced"
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    assert bpp in (3, 4), f"{path}: expect RGB or RGBA"
    raw = zlib.decompress(idat)
    stride = w * bpp
    img = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        img[y * stride:(y + 1) * stride] = line
        prev = line
    px = [(img[y * stride + x * bpp], img[y * stride + x * bpp + 1], img[y * stride + x * bpp + 2])
          for y in range(h) for x in range(w)]
    return w, h, px


def untop(px, w, h):
    """Off the top, the black band the artwork was drawn with: on screen it
    is a gap between the top of the picture and the picture itself."""
    first = 0
    for y in range(h):
        row = px[y * w:(y + 1) * w]
        if sum(sum(c) for c in row) / (w * 3.0) >= 12:
            first = y
            break
    return px[first * w:], h - first


def fade(px, w, h):
    """Bright to the waist, then away to black by the bottom edge, so the
    artwork dissolves into the screen instead of being washed all over."""
    out = []
    for y in range(h):
        f = (y / (h - 1.0) - FADE_FROM) / (1.0 - FADE_FROM)
        keep = 1.0 if f <= 0.0 else (1.0 - f) ** 1.4
        for x in range(w):
            r, g, b = px[y * w + x]
            out.append(tuple(min(255, int(v * LIFT * keep + 0.5)) for v in (r, g, b)))
    return out


def median_cut(hist, want):
    """hist: {(r,g,b): count}.  Returns up to `want` colours, the average of
    each box, boxes split on their longest axis at the weighted median."""
    boxes = [list(hist.items())]
    while len(boxes) < want:
        # the box worth splitting: most pixels, and more than one colour in it
        cand = [b for b in boxes if len(b) > 1]
        if not cand:
            break
        box = max(cand, key=lambda b: sum(c for _, c in b))
        boxes.remove(box)
        lo = [min(c[0][i] for c in box) for i in range(3)]
        hi = [max(c[0][i] for c in box) for i in range(3)]
        axis = max(range(3), key=lambda i: hi[i] - lo[i])
        box.sort(key=lambda kv: (kv[0][axis], kv[0]))
        half = sum(c for _, c in box) / 2.0
        run, cut = 0, 1
        for i, (_, c) in enumerate(box):
            run += c
            if run >= half:
                cut = max(1, min(len(box) - 1, i))
                break
        boxes.append(box[:cut])
        boxes.append(box[cut:])
    pal = []
    for box in boxes:
        n = sum(c for _, c in box)
        pal.append(tuple(int(sum(k[i] * c for k, c in box) / n + 0.5) for i in range(3)))
    return pal


def quantise(px, want):
    hist = {}
    for p in px:
        hist[p] = hist.get(p, 0) + 1
    pal = median_cut(hist, want)
    # every colour in the image maps once, then the pixels are a lookup
    near = {}
    for colour in hist:
        best, bd = 0, 1 << 30
        for i, p in enumerate(pal):
            d = ((colour[0] - p[0]) ** 2) * 3 + ((colour[1] - p[1]) ** 2) * 6 + (
                (colour[2] - p[2]) ** 2)
            if d < bd:
                best, bd = i, d
        near[colour] = best
    return pal, bytes(near[p] for p in px)


def main():
    out_c, out_h, srcs = sys.argv[1], sys.argv[2], sys.argv[3:]
    names, blobs = [], []
    for src in srcs:
        name = src.split('/')[-1].rsplit('.', 1)[0]
        w, h, px = read_png(src)
        assert (w, h) == (SRC_W, SRC_H), f"{src}: expect {SRC_W}x{SRC_H}, got {w}x{h}"
        small, hh = untop(px, W, H)
        pal, idx = quantise(fade(small, W, hh), PALETTE)
        names.append(name)
        blobs.append((pal, idx, hh))

    with open(out_c, 'w') as f:
        f.write("/* generated by tools/mkbanners.py from assets/banners - do not edit */\n")
        f.write('#include "banners.h"\n')
        for name, (pal, idx, hh) in zip(names, blobs):
            f.write(f"\nstatic const uint8_t px_{name}[] = {{")
            f.write(','.join(str(b) for b in idx))
            f.write("};\n")
            f.write(f"static const uint8_t pal_{name}[] = {{")
            f.write(','.join(str(c) for p in pal for c in p))
            f.write("};\n")
        f.write("\nconst dxm_banner dxm_banners[] = {\n")
        for name, (_, _, hh) in zip(names, blobs):
            f.write(f'    {{"{name}", px_{name}, pal_{name}, {hh}}},\n')
        f.write("};\n")
        f.write(f"const int dxm_banner_count = {len(names)};\n")

    with open(out_h, 'w') as f:
        f.write("/* generated by tools/mkbanners.py from assets/banners - do not edit */\n")
        f.write("#ifndef DXM_BANNERS_H_INCLUDED\n#define DXM_BANNERS_H_INCLUDED\n")
        f.write("#include <stdint.h>\n")
        f.write(f"#define DXM_BANNER_W {W}\n#define DXM_BANNER_MAX_H {H}\n")
        f.write(f"#define DXM_BANNER_FIRST {UI_COLOURS} /* the first palette entry it owns */\n")
        f.write(f"#define DXM_BANNER_COLOURS {PALETTE}\n")
        f.write("/* one piece of artwork: DXM_BANNER_W*h palette indices, each\n"
                " * 0..DXM_BANNER_COLOURS-1, and DXM_BANNER_COLOURS RGB triples.  The\n"
                " * height varies: the black band each was drawn with is trimmed off. */\n")
        f.write("typedef struct {\n    const char *name;\n    const uint8_t *px;\n"
                "    const uint8_t *pal;\n    int h;\n} dxm_banner;\n")
        f.write("extern const dxm_banner dxm_banners[];\nextern const int dxm_banner_count;\n")
        f.write("#endif\n")


main()
