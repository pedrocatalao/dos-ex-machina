#!/usr/bin/env python3
"""mkico.py <in.png> <out.ico> — build a Windows icon from a square PNG.

A modern .ico is just a small directory followed by the image data, and
Vista onward accepts PNG directly for each entry rather than the old DIB
format.  That makes this short enough to carry, and means no image tooling
has to exist on the Windows runner - the .ico is generated once, here, and
committed.

The sizes are the ones Explorer actually asks for: 16 and 32 in lists, 48
in medium view, 256 for the large tiles and the taskbar on a HiDPI screen.
"""
import struct, sys, zlib

SIZES = [16, 32, 48, 64, 128, 256]

def read_png(path):
    d = open(path, 'rb').read()
    pos, idat = 8, b''
    while pos < len(d):
        ln, = struct.unpack('>I', d[pos:pos+4]); typ = d[pos+4:pos+8]
        if typ == b'IHDR':
            w, h, bd, ct, _, _, inter = struct.unpack('>IIBBBBB', d[pos+8:pos+8+13])
        elif typ == b'IDAT':
            idat += d[pos+8:pos+8+ln]
        pos += 12 + ln
    assert bd == 8 and ct == 6 and inter == 0, "expect 8-bit RGBA, non-interlaced"
    raw = zlib.decompress(idat); stride = w * 4
    img = bytearray(h*stride); prev = bytearray(stride); p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p+stride]); p += stride
        if f == 1:
            for i in range(4, stride): line[i] = (line[i]+line[i-4]) & 255
        elif f == 2:
            for i in range(stride): line[i] = (line[i]+prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i-4] if i >= 4 else 0
                line[i] = (line[i] + ((a+prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i-4] if i >= 4 else 0
                b = prev[i]; c = prev[i-4] if i >= 4 else 0
                pp = a+b-c; pa = abs(pp-a); pb = abs(pp-b); pc = abs(pp-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i]+pr) & 255
        img[y*stride:(y+1)*stride] = line; prev = line
    return img, w, h

def box(src, w, h, n):
    """Downscale to n*n, averaging in premultiplied space so the transparent
    edge pixels do not drag their colour into the result."""
    out = bytearray(n*n*4)
    for y in range(n):
        y0 = y*h//n; y1 = max(y0+1, (y+1)*h//n)
        for x in range(n):
            x0 = x*w//n; x1 = max(x0+1, (x+1)*w//n)
            sr = sg = sb = sa = cnt = 0
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    o = (yy*w+xx)*4; a = src[o+3]
                    sr += src[o]*a; sg += src[o+1]*a; sb += src[o+2]*a
                    sa += a; cnt += 1
            o = (y*n+x)*4
            if sa:
                out[o] = sr//sa; out[o+1] = sg//sa; out[o+2] = sb//sa
            out[o+3] = sa//cnt
    return bytes(out)

def png_bytes(px, n):
    def chunk(t, data):
        return (struct.pack('>I', len(data)) + t + data +
                struct.pack('>I', zlib.crc32(t+data) & 0xffffffff))
    raw = b''.join(b'\x00' + px[y*n*4:(y+1)*n*4] for y in range(n))
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', n, n, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9))
            + chunk(b'IEND', b''))

src_path, out_path = sys.argv[1], sys.argv[2]
img, w, h = read_png(src_path)
assert w == h, "icon must be square"

images = [png_bytes(box(img, w, h, n), n) for n in SIZES]

# ICONDIR, then one ICONDIRENTRY each, then the data
out = struct.pack('<HHH', 0, 1, len(SIZES))
offset = 6 + 16*len(SIZES)
for n, data in zip(SIZES, images):
    out += struct.pack('<BBBBHHII',
                       0 if n >= 256 else n,     # 0 means 256
                       0 if n >= 256 else n,
                       0, 0, 1, 32, len(data), offset)
    offset += len(data)
out += b''.join(images)
open(out_path, 'wb').write(out)
print(f"{out_path}: {len(SIZES)} sizes {SIZES}, {len(out)} bytes")
