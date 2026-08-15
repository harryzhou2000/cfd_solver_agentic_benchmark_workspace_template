"""Tiny pure-stdlib raster canvas: lines, filled polygons, bitmap text."""

from png_writer import write_png
from font5x7 import FONT


class Canvas:
    def __init__(self, width, height, bg=(255, 255, 255, 255)):
        self.w = int(width)
        self.h = int(height)
        self.buf = bytearray(self.w * self.h * 4)
        self.fill(0, 0, self.w, self.h, bg)

    def _idx(self, x, y):
        return (int(y) * self.w + int(x)) * 4

    def fill(self, x0, y0, x1, y1, color):
        x0 = max(0, int(x0)); y0 = max(0, int(y0))
        x1 = min(self.w, int(x1)); y1 = min(self.h, int(y1))
        if x1 <= x0 or y1 <= y0:
            return
        r, g, b, a = (int(c) for c in color)
        buf = self.buf
        w = self.w
        block = bytes((r, g, b, a)) * (x1 - x0)
        for y in range(y0, y1):
            base = (y * w + x0) * 4
            buf[base:base + 4 * (x1 - x0)] = block

    def set_pixel(self, x, y, color):
        if 0 <= int(x) < self.w and 0 <= int(y) < self.h:
            idx = self._idx(x, y)
            self.buf[idx] = int(color[0]); self.buf[idx + 1] = int(color[1])
            self.buf[idx + 2] = int(color[2]); self.buf[idx + 3] = int(color[3])

    def line(self, x0, y0, x1, y1, color, width=1):
        x0 = int(round(x0)); y0 = int(round(y0))
        x1 = int(round(x1)); y1 = int(round(y1))
        w = max(1, int(width))
        r, g, b, a = (int(c) for c in color)
        buf = self.buf
        W = self.w
        H = self.h
        oxs = list(range(-(w // 2), w - w // 2))
        dx = abs(x1 - x0); dy = -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            for ox in oxs:
                px = x0 + ox
                if 0 <= px < W:
                    for oy in oxs:
                        py = y0 + oy
                        if 0 <= py < H:
                            idx = (py * W + px) * 4
                            buf[idx] = r; buf[idx + 1] = g
                            buf[idx + 2] = b; buf[idx + 3] = a
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy; x0 += sx
            if e2 <= dx:
                err += dx; y0 += sy

    def polyline(self, pts, color, width=1):
        for i in range(len(pts) - 1):
            self.line(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], color, width)

    def rect(self, x0, y0, x1, y1, color, width=1):
        self.line(x0, y0, x1, y0, color, width)
        self.line(x1, y0, x1, y1, color, width)
        self.line(x1, y1, x0, y1, color, width)
        self.line(x0, y1, x0, y0, color, width)

    def text(self, x, y, s, color=(0, 0, 0, 255), scale=2):
        cx = int(x)
        cy = int(y)
        for ch in s:
            glyph = FONT.get(ch, FONT[' '])
            for r, row in enumerate(glyph):
                for c, pix in enumerate(row):
                    if pix == '#':
                        self.fill(cx + c * scale, cy + r * scale,
                                  cx + (c + 1) * scale, cy + (r + 1) * scale, color)
            cx += 6 * scale

    def text_center(self, cx, y, s, color=(0, 0, 0, 255), scale=2):
        wpx = sum(6 for _ in s) * scale
        self.text(cx - wpx / 2, y, s, color, scale)

    def fill_polygon(self, pts, color):
        ys = [p[1] for p in pts]
        y0 = max(0, int(min(ys)))
        y1 = min(self.h, int(max(ys)) + 1)
        if y1 <= y0:
            return
        edges = []
        n = len(pts)
        for i in range(n):
            xa, ya = pts[i]
            xb, yb = pts[(i + 1) % n]
            edges.append((xa, ya, xb, yb))
        r, g, b, a = (int(c) for c in color)
        buf = self.buf
        w = self.w
        for y in range(y0, y1):
            xs = []
            yc = y + 0.5
            for xa, ya, xb, yb in edges:
                if (ya <= yc < yb) or (yb <= yc < ya):
                    t = (yc - ya) / (yb - ya)
                    xs.append(xa + t * (xb - xa))
            xs.sort()
            for k in range(0, len(xs) - 1, 2):
                xl = max(0, int(xs[k]))
                xr = min(w, int(xs[k + 1]) + 1)
                if xr <= xl:
                    continue
                base = (y * w + xl) * 4
                buf[base:base + 4 * (xr - xl)] = bytes((r, g, b, a)) * (xr - xl)

    def save(self, path):
        rows = []
        for y in range(self.h):
            row = []
            base = y * self.w * 4
            for x in range(self.w):
                idx = base + x * 4
                row.append((self.buf[idx], self.buf[idx + 1],
                            self.buf[idx + 2], self.buf[idx + 3]))
            rows.append(row)
        write_png(path, self.w, self.h, rows)
