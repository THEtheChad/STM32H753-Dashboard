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

def run(cli, args, check=True):
    r = subprocess.run([cli, "-c", "port=SWD", "mode=HOTPLUG"] + args,
                       capture_output=True, text=True)
    if check and ("Error" in r.stdout or r.returncode != 0):
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
    for i in range(1, 16):              # mirror at {i,i} like the firmware table
        if clut[i] and not clut[i * 17]:
            clut[i * 17] = clut[i]
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
    s0_bin, s1_bin = os.path.join(tmp, "s0.bin"), os.path.join(tmp, "s1.bin")
    fb_bin = os.path.join(tmp, "fb.bin")
    SPR0, SPR1 = 0x24000000, 0x24020000          # sprite ping-pong buffers
    SPR_BYTES = SPR_PITCH * SPR_PITCH

    # ONE connection, target halted: registers and both sprite buffers are
    # captured as a single consistent instant (a second connection would be
    # ~1 s later — by then the back buffer is mid-redraw for a newer frame).
    out1 = run(cli, ["-halt", "-r32", hex(LTDC), "0x140",
                     "-u", hex(SPR0), hex(SPR_BYTES), s0_bin,
                     "-u", hex(SPR1), hex(SPR_BYTES), s1_bin, "-g"])
    out1 = re.sub(r"\x1b\[[0-9;]*m", "", out1)
    regs = {}
    for line in out1.splitlines():
        m = re.search(r"(0x[0-9A-F]{8})\s+:\s+(.*)", line.strip(), re.I)
        if m:
            base = int(m.group(1), 16)
            for k, w in enumerate(m.group(2).split()):
                regs[base + 4*k] = int(w, 16)

    isr   = regs[LTDC + 0x38]
    bpcr  = regs[LTDC + 0x0C]
    fb_addr = regs[LTDC + 0xAC]                   # layer 1 face (flash or RAM)
    l2cr  = regs[LTDC + 0x104]
    whpcr, wvpcr = regs[LTDC + 0x108], regs[LTDC + 0x10C]
    cfbar = regs[LTDC + 0x12C]
    if isr & 0x6:
        print(f"note: LTDC ISR=0x{isr:X} at capture "
              "(can be residue of a previous dump; rerun to confirm live)")

    ahbp, avbp = (bpcr >> 16) & 0xFFF, bpcr & 0x7FF
    x0 = (whpcr & 0xFFFF) - ahbp - 1
    y0 = (wvpcr & 0xFFFF) - avbp - 1
    sw = (whpcr >> 16) - ahbp - x0
    sh = (wvpcr >> 16) - avbp - y0

    # face is static (flash) — a second connection cannot race anything
    run(cli, ["-u", hex(fb_addr), hex(FB_BYTES), fb_bin])
    run(cli, ["-w32", hex(LTDC + 0x3C), "0xE"], check=False)  # clear dump-tripped flags

    fb  = open(fb_bin, "rb").read()
    spr = open(s0_bin if cfbar == SPR0 else s1_bin, "rb").read()
    clut = parse_clut()

    rgb = bytearray(W * H * 3)
    for i, p in enumerate(fb):
        rgb[i*3:i*3+3] = clut[p].to_bytes(3, "big")
    if l2cr & 1:
        for r in range(sh):
            for c in range(sw):
                v = spr[r * SPR_PITCH + c]
                if v >> 4:                        # AL44 alpha nibble
                    i = ((y0 + r) * W + (x0 + c)) * 3
                    rgb[i:i+3] = clut[(v & 0x0F) * 17].to_bytes(3, "big")

    png_write(out, W, H, bytes(rgb))
    print(f"wrote {out}  (sprite window {sw}x{sh} at {x0},{y0}, buffer 0x{cfbar:08X}, ISR=0x{isr:X})")

if __name__ == "__main__":
    main()
