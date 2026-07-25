#!/usr/bin/env python3
"""Screenshot the live gauge over SWD.

Halts the MCU briefly, dumps LTDC layer 1 (face framebuffer) and the
currently displayed layer 2 needle sprite plus the layer registers,
resumes the MCU, then composites everything through the CLUT parsed
from Display/gauge.c into a PNG.

Usage: python3 tools/fbshot.py [out.png]
"""
import glob
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W = H = 720
FB_ADDR, FB_BYTES = 0x24000000, W * H
LTDC = 0x50001000
SPR_PITCH = 352

def find_cli():
    pats = glob.glob(os.path.expanduser(
        "~/Library/Application Support/stm32cube/bundles/programmer/*/bin/STM32_Programmer_CLI"))
    if not pats:
        sys.exit("STM32_Programmer_CLI not found (install the ST VS Code bundles)")
    return sorted(pats)[-1]

def run(cli, args):
    r = subprocess.run([cli, "-c", "port=SWD", "mode=HOTPLUG"] + args,
                       capture_output=True, text=True)
    if "Error" in r.stdout or r.returncode != 0:
        sys.exit(f"probe error (is a debug session holding the ST-LINK?):\n{r.stdout[-400:]}")
    return r.stdout

def read_words(cli, addr, n):
    out = run(cli, ["-r32", hex(addr), hex(n * 4)])
    out = re.sub(r"\x1b\[[0-9;]*m", "", out)          # strip ANSI colors
    words = []
    for line in out.splitlines():
        m = re.search(r"0x[0-9A-F]+\s+:\s+(.*)", line.strip(), re.I)
        if m:
            words += [int(w, 16) for w in m.group(1).split()]
    return words[:n]

def parse_clut():
    src = open(os.path.join(REPO, "Display", "gauge.c")).read()
    clut = [0] * 256
    body = re.search(r"Gauge_CLUT\[256\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
    names = dict(re.findall(r"(C_\w+)\s*=\s*(\d+)", src))
    for name, val in re.findall(r"\[(C_\w+)\]\s*=\s*0x([0-9A-Fa-f]{6})", body):
        clut[int(names[name])] = int(val, 16)
    return clut

def png_write(path, w, h, rgb):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))
    raw = b"".join(b"\x00" + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b""))

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "fbshot.png"
    cli = find_cli()
    tmp = tempfile.mkdtemp()
    fb_bin = os.path.join(tmp, "fb.bin")
    spr_bin = os.path.join(tmp, "spr.bin")

    # layer 2 shadow-visible registers: CR, WHPCR, WVPCR, PFCR @ 0x104.., CFBAR/CFBLR/CFBLNR @ 0x12C..
    l2 = read_words(cli, LTDC + 0x104, 4)
    cfb = read_words(cli, LTDC + 0x12C, 3)
    bpcr = read_words(cli, LTDC + 0x0C, 1)[0]
    ahbp, avbp = (bpcr >> 16) & 0xFFF, bpcr & 0x7FF
    l2cr, whpcr, wvpcr = l2[0], l2[1], l2[2]
    cfbar, cfblr, cfblnr = cfb[0], cfb[1], cfb[2]
    x0 = (whpcr & 0xFFFF) - ahbp - 1
    x1 = (whpcr >> 16) - ahbp          # exclusive
    y0 = (wvpcr & 0xFFFF) - avbp - 1
    y1 = (wvpcr >> 16) - avbp
    sw, sh = x1 - x0, y1 - y0

    # halt -> dump both buffers -> resume (LTDC keeps scanning while halted)
    run(cli, ["-halt", "-u", hex(FB_ADDR), hex(FB_BYTES), fb_bin,
              "-u", hex(cfbar), hex(SPR_PITCH * max(sh, 1)), spr_bin, "-g"])

    fb = open(fb_bin, "rb").read()
    spr = open(spr_bin, "rb").read()
    clut = parse_clut()

    rgb = bytearray(W * H * 3)
    for i, p in enumerate(fb):
        rgb[i*3:i*3+3] = clut[p].to_bytes(3, "big")
    if l2cr & 1:                        # layer 2 enabled: composite AL44 sprite
        for r in range(sh):
            for c in range(sw):
                v = spr[r * SPR_PITCH + c]
                if v >> 4:              # alpha nibble (needle uses 0xF)
                    i = ((y0 + r) * W + (x0 + c)) * 3
                    rgb[i:i+3] = clut[v & 0x0F].to_bytes(3, "big")

    png_write(out, W, H, bytes(rgb))
    print(f"wrote {out}  (sprite window {sw}x{sh} at {x0},{y0}, buffer 0x{cfbar:08X})")

if __name__ == "__main__":
    main()
