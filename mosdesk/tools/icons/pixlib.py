"""Tiny pixel-art toolkit: aliased shapes, ramps, auto-outline, drop shadow."""
from PIL import Image, ImageDraw

S = 64

def hexc(h, a=255):
    h = h.lstrip('#')
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)

class Canvas:
    def __init__(self):
        self.img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    # -- primitives (all aliased: crisp pixel art) --------------------------
    def px(self, x, y, c):
        if 0 <= x < S and 0 <= y < S:
            self.img.putpixel((x, y), hexc(c) if isinstance(c, str) else c)

    def rect(self, x0, y0, x1, y1, c):
        self.d.rectangle([x0, y0, x1, y1], fill=hexc(c))

    def rrect(self, x0, y0, x1, y1, r, c):
        self.d.rounded_rectangle([x0, y0, x1, y1], r, fill=hexc(c))

    def poly(self, pts, c):
        self.d.polygon(pts, fill=hexc(c))

    def ell(self, x0, y0, x1, y1, c):
        self.d.ellipse([x0, y0, x1, y1], fill=hexc(c))

    def line(self, pts, c, w=1):
        self.d.line(pts, fill=hexc(c), width=w)

    def hline(self, x0, x1, y, c):
        for x in range(x0, x1 + 1): self.px(x, y, c)

    def vline(self, x, y0, y1, c):
        for y in range(y0, y1 + 1): self.px(x, y, c)

    def vgrad(self, x0, y0, x1, y1, ramp, mask=None):
        """Banded vertical gradient (pixel-art style, hard bands)."""
        h = y1 - y0 + 1
        for y in range(y0, y1 + 1):
            t = (y - y0) / max(1, h - 1)
            c = ramp[min(len(ramp) - 1, int(t * len(ramp)))]
            for x in range(x0, x1 + 1):
                if mask is None or mask(x, y):
                    self.px(x, y, c)

    def dither(self, x0, y0, x1, y1, c, phase=0, mask=None):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if (x + y + phase) % 2 == 0 and (mask is None or mask(x, y)):
                    self.px(x, y, c)

    def alpha_at(self, x, y):
        if 0 <= x < S and 0 <= y < S:
            return self.img.getpixel((x, y))[3]
        return 0

    # -- finishing ----------------------------------------------------------
    def outline(self, color, inner=None):
        """1px outline hugging the silhouette. `inner` darkens edge pixels."""
        src = self.img.copy()
        out = self.img
        oc = hexc(color)
        for y in range(S):
            for x in range(S):
                if src.getpixel((x, y))[3] == 0:
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < S and 0 <= ny < S and src.getpixel((nx, ny))[3] > 0:
                            out.putpixel((x, y), oc)
                            break

    def shadow(self, dx=2, dy=2, alpha=70, color=(10, 18, 30)):
        """Soft ground shadow behind the silhouette (kept under the art)."""
        src = self.img.copy()
        sh = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        for y in range(S):
            for x in range(S):
                if src.getpixel((x, y))[3] > 0:
                    sx, sy = x + dx, y + dy
                    if 0 <= sx < S and 0 <= sy < S:
                        sh.putpixel((sx, sy), color + (alpha,))
        sh.alpha_composite(src)
        self.img = sh
        self.d = ImageDraw.Draw(self.img)

def lighten(c, f):
    r, g, b, a = hexc(c)
    return '#%02x%02x%02x' % tuple(int(v + (255 - v) * f) for v in (r, g, b))

def darken(c, f):
    r, g, b, a = hexc(c)
    return '#%02x%02x%02x' % tuple(int(v * (1 - f)) for v in (r, g, b))
