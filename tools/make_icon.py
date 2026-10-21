#!/usr/bin/env python3
"""Draws pkgfiles/ICON0.PNG (the 320x176 XMB icon) with no dependencies.

Run from the repository root: python3 tools/make_icon.py
"""
import math
import struct
import zlib

W, H = 320, 176
BG = (10, 15, 30)

GLYPHS = {
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
}

px = [[BG for _ in range(W)] for _ in range(H)]


def blend(x, y, color, alpha=1.0):
    if 0 <= x < W and 0 <= y < H:
        r, g, b = px[y][x]
        px[y][x] = tuple(int(c0 * (1 - alpha) + c1 * alpha) for c0, c1 in zip((r, g, b), color))


def cover(x0, x1, h0, h1, cy, body, band):
    """A cover seen at an angle: edge heights h0 (left) and h1 (right)."""
    for x in range(x0, x1 + 1):
        t = (x - x0) / max(1, x1 - x0)
        h = h0 + (h1 - h0) * t
        top, bottom = int(cy - h / 2), int(cy + h / 2)
        band_end = top + int(h * 0.11)
        for y in range(top, bottom + 1):
            blend(x, y, band if y <= band_end else body)


def waves():
    for k, (color, alpha) in enumerate([((59, 79, 158), 0.55), ((90, 111, 196), 0.35), ((59, 79, 158), 0.25)]):
        for x in range(W):
            y = 128 + k * 9 + math.sin(x * 0.03 + k * 1.3) * (10 - k * 2)
            blend(x, int(y), color, alpha)


def text(s, cx, y, scale, color):
    width = len(s) * 6 * scale - scale
    x = cx - width // 2
    for ch in s:
        for row, bits in enumerate(GLYPHS[ch]):
            for col, bit in enumerate(bits):
                if bit == "1":
                    for dy in range(scale):
                        for dx in range(scale):
                            blend(x + col * scale + dx, y + row * scale + dy, color)
        x += 6 * scale


waves()
cover(58, 104, 50, 68, 64, (42, 53, 80), (17, 17, 17))
cover(216, 262, 68, 50, 64, (42, 53, 80), (17, 17, 17))
cover(124, 196, 100, 100, 64, (58, 111, 216), (17, 17, 17))
text("PS3SX2", W // 2, 136, 3, (232, 236, 245))


def chunk(tag, data):
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in px)
png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw, 9))
       + chunk(b"IEND", b""))

with open("pkgfiles/ICON0.PNG", "wb") as f:
    f.write(png)
print("wrote pkgfiles/ICON0.PNG")
