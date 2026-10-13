#!/usr/bin/env python3
"""Composes the Posy cursor showcase images from the harness dump.

usage: cursor_showcase.py <dump-dir> <Posy-pack-dir> <out-dir>
Everything marked "WM" below is the sprite the REAL mosWM loader decoded
(32x32 entry picked from the .cur/.ani); "source" images are the 128x128
entries shipped in the pack.
"""
import io, os, struct, sys
from PIL import Image, ImageDraw, ImageFont

dump, pack, out = sys.argv[1:4]
HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, '..', '..', 'fonts')
os.makedirs(out, exist_ok=True)

def font(name, size):
    try: return ImageFont.truetype(os.path.join(FONTS, name), size)
    except Exception: return ImageFont.load_default()
F_TITLE = font('Inter-Medium.otf', 26); F_H = font('Inter-Medium.otf', 15)
F_S = font('Inter-Regular.otf', 11); F_M = font('Inter-Regular.otf', 12)

BG = (16, 24, 32); PANEL = (23, 31, 40); EDGE = (68, 82, 93)
ACCENT = (85, 168, 220); TEXT = (232, 238, 242); MUTED = (140, 160, 174)

NAMES = [  # status, label, file, caption(context)
    ('Arrow', 'arrow.cur', 'Desktop'), ('Busy', 'busy.ani', 'App is busy'),
    ('Working', 'working.ani', 'Working in background'), ('Text', 'text.cur', 'Over editable text'),
    ('Hand', 'hand.cur', 'Over a swatch / button'), ('Help', 'help.cur', 'Help available'),
    ('Unavailable', 'forbidden.cur', 'Action not allowed'), ('Move', 'move.cur', 'Window title bar'),
    ('Resize E-W', 'resize-ew.cur', 'Vertical divider'), ('Resize NE-SW', 'resize-nesw.cur', 'Diagonal edge'),
    ('Resize N-S', 'resize-ns.cur', 'Horizontal divider'), ('Resize NW-SE', 'resize-nwse.cur', 'Window corner'),
    ('Precision', 'precise.cur', 'Precise pick'), ('Pen', 'pen.cur', 'Pen input')]
AT = [(930,150),(520,262),(520,262),(352,179),(376,221),(350,301),(331,340),
      (330,94),(289,262),(620,170),(470,152),(655,404),(520,339),(430,339)]

# ---- load the sprites the real WM decoded ---------------------------------
raw = open(os.path.join(dump, 'sprites.bin'), 'rb').read()
sprites = {}; o = 0
while o < len(raw):
    st, w, h, hx, hy, n, d, _ = struct.unpack_from('<8I', raw, o); o += 32
    frames = []
    for k in range(n):
        buf = raw[o:o + 4096]; o += 4096
        # ARGB uint32 little-endian == BGRA bytes
        im = Image.frombuffer('RGBA', (32, 32), buf, 'raw', 'BGRA', 0, 1).copy()
        frames.append(im)
    sprites[st] = dict(w=w, h=h, hx=hx, hy=hy, frames=frames, delay=d)

def tint(im, fill):
    """What the WM does: near-white pixels take the chosen cursor fill colour."""
    px = im.load(); r2 = im.copy(); p2 = r2.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a and r > 210 and g > 210 and b > 210:
                p2[x, y] = (fill[0], fill[1], fill[2], a)
    return r2

def up(im, k): return im.resize((im.width * k, im.height * k), Image.NEAREST)

def wallpaper_box(w, h):
    box = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(box)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=(int(21 + 14 * t), int(40 + 20 * t), int(58 + 28 * t), 255))
    return box

def checker(w, h, a=(236, 240, 243), b=(222, 228, 232), s=8):
    box = Image.new('RGBA', (w, h)); d = ImageDraw.Draw(box)
    for y in range(0, h, s):
        for x in range(0, w, s):
            d.rectangle([x, y, x + s - 1, y + s - 1], fill=a if ((x // s) + (y // s)) % 2 == 0 else b)
    return box

def place(canvas, sp, frame, cx, cy, k, mark=False, color=ACCENT):
    """Paste sprite scaled by k so its hotspot lands on (cx, cy)."""
    im = up(sp['frames'][frame % len(sp['frames'])], k)
    ox, oy = cx - sp['hx'] * k, cy - sp['hy'] * k
    canvas.alpha_composite(im, (ox, oy)) if ox >= 0 and oy >= 0 else canvas.paste(im, (ox, oy), im)
    if mark:
        d = ImageDraw.Draw(canvas)
        d.rectangle([cx - 3, cy - 3, cx + 3, cy + 3], outline=(255, 90, 80, 255))
        d.line([(cx - 8, cy), (cx + 8, cy)], fill=(255, 90, 80, 255)); d.line([(cx, cy - 8), (cx, cy + 8)], fill=(255, 90, 80, 255))

def bbox(sp):
    """Bounding box of the opaque pixels of the first frame."""
    return sp['frames'][0].getchannel('A').getbbox()

# ===================== 1. all 14 states (what the WM draws) ================
def sheet_states():
    cols, rows = 5, 3
    TW, TH, GAP = 300, 262, 14
    W = cols * TW + (cols + 1) * GAP; H = 112 + rows * TH + (rows + 1) * GAP + 56
    img = Image.new('RGBA', (W, H), BG + (255,)); d = ImageDraw.Draw(img)
    d.text((GAP + 6, 22), 'Posy cursor pack', font=F_TITLE, fill=TEXT)
    d.text((GAP + 6, 58), '14 cursor states, as decoded and drawn by the real mosWM compositor  \u00b7  32\u00d732 sprites, hotspot marked in red',
           font=F_M, fill=MUTED)
    top = 100
    for i in range(14):
        r, c = divmod(i, cols)
        x0 = GAP + c * (TW + GAP); y0 = top + GAP + r * (TH + GAP)
        d.rounded_rectangle([x0, y0, x0 + TW - 1, y0 + TH - 1], 10, fill=PANEL + (255,), outline=EDGE + (255,))
        label, fname, ctxt = NAMES[i]; sp = sprites[i]
        d.text((x0 + 14, y0 + 11), label, font=F_H, fill=TEXT)
        meta = 'status %d \u00b7 %s' % (i, fname)
        d.text((x0 + TW - 14 - d.textlength(meta, font=F_S), y0 + 15), meta, font=F_S, fill=MUTED)
        # big 4x preview
        bx, by, bw, bh = x0 + 14, y0 + 40, 148, 148
        big = wallpaper_box(bw, bh)
        place(big, sp, 0, bw // 2 - (sp['w'] // 2 - sp['hx']) * 4 + 0, bh // 2 - (sp['h'] // 2 - sp['hy']) * 4, 4, mark=True)
        img.alpha_composite(big, (bx, by))
        d.rectangle([bx, by, bx + bw - 1, by + bh - 1], outline=EDGE + (255,))
        # actual-size boxes (1x) on dark + light
        rx = bx + bw + 10
        for j, kind in enumerate(('dark', 'light')):
            b = wallpaper_box(56, 56) if kind == 'dark' else checker(56, 56)
            place(b, sp, 0, 28 - (sp['w'] // 2 - sp['hx']), 28 - (sp['h'] // 2 - sp['hy']), 1)
            img.alpha_composite(b, (rx + j * 60, by))
            d.rectangle([rx + j * 60, by, rx + j * 60 + 55, by + 55], outline=EDGE + (255,))
        d.text((rx, by + 59), 'actual size', font=F_S, fill=MUTED)
        # 2x scale (Settings > Cursor > Size 200%)
        b = wallpaper_box(116, 70)
        place(b, sp, 0, 58 - (sp['w'] // 2 - sp['hx']) * 2, 35 - (sp['h'] // 2 - sp['hy']) * 2, 2)
        img.alpha_composite(b, (rx, by + 78)); d.rectangle([rx, by + 78, rx + 115, by + 147], outline=EDGE + (255,))
        d.text((rx + 4, by + 150 - 14), '200%', font=F_S, fill=(150, 175, 195))
        # footer
        fy = y0 + 200
        if len(sp['frames']) > 1:
            n = len(sp['frames']); picks = [int(n * k / 8) for k in range(8)]
            for k, fi in enumerate(picks):
                b = wallpaper_box(32, 32)
                place(b, sp, fi, 16 - (sp['w'] // 2 - sp['hx']), 16 - (sp['h'] // 2 - sp['hy']), 1)
                img.alpha_composite(b, (x0 + 14 + k * 34, fy))
            d.text((x0 + 14, fy + 38), '%d frames \u00b7 %d ms each \u00b7 loops in %.2f s' % (n, sp['delay'], n * sp['delay'] / 1000),
                   font=F_S, fill=ACCENT + (255,))
        else:
            d.text((x0 + 14, fy + 2), 'Used for: ' + ctxt, font=F_M, fill=(190, 205, 215))
            d.text((x0 + 14, fy + 24), 'hotspot (%d, %d) \u00b7 sprite %d\u00d7%d' % (sp['hx'], sp['hy'], sp['w'], sp['h']),
                   font=F_S, fill=MUTED)
            bb = bbox(sp)
            d.text((x0 + 14, fy + 40), 'opaque area %d\u00d7%d px' % (bb[2] - bb[0], bb[3] - bb[1]), font=F_S, fill=MUTED)
    # fill-colour strip (Settings > Cursor > Cursor fill) in last slot
    r, c = divmod(14, cols)
    x0 = GAP + c * (TW + GAP); y0 = top + GAP + r * (TH + GAP)
    d.rounded_rectangle([x0, y0, x0 + TW - 1, y0 + TH - 1], 10, fill=PANEL + (255,), outline=EDGE + (255,))
    d.text((x0 + 14, y0 + 11), 'Cursor fill', font=F_H, fill=TEXT)
    d.text((x0 + 14, y0 + 33), 'Settings recolours the white parts', font=F_S, fill=MUTED)
    fills = [('White', (255, 255, 255)), ('Cream', (255, 228, 166)), ('Sky', (142, 197, 255)), ('Mint', (85, 214, 190))]
    for k, (nm, col) in enumerate(fills):
        bx = x0 + 14 + (k % 2) * 138; by = y0 + 62 + (k // 2) * 94
        b = wallpaper_box(126, 84)
        place(b, {**sprites[0], 'frames': [tint(sprites[0]['frames'][0], col)]}, 0, 40, 16, 4)
        place(b, {**sprites[4], 'frames': [tint(sprites[4]['frames'][0], col)]}, 0, 100, 18, 2)
        img.alpha_composite(b, (bx, by)); d.rectangle([bx, by, bx + 125, by + 83], outline=EDGE + (255,))
        d.text((bx + 5, by + 68), nm, font=F_S, fill=(200, 212, 222))
    # footer note
    d.text((GAP + 6, H - 34), 'Rendered with mosdesk/tools/host_preview/cursors.sh \u2014 the harness compiles mosWM/main.c and calls its own ensure_cursor_loaded() for each status.',
           font=F_S, fill=MUTED)
    img.convert('RGB').save(os.path.join(out, 'posy_cursor_states.png'))

# ===================== 2. in context (real compositor frames) ==============
def read_ppm(path):
    with open(path, 'rb') as f:
        magic, w, h, _ = f.readline().split()      # "P6 <w> <h> 255"
        assert magic == b'P6'
        return Image.frombytes('RGB', (int(w), int(h)), f.read())

def sheet_context():
    cols, rows = 4, 4
    CW, CH, K = 170, 112, 2
    TW, TH, GAP = CW * K + 24, CH * K + 56, 12
    W = cols * TW + (cols + 1) * GAP; H = 100 + rows * TH + (rows + 1) * GAP
    img = Image.new('RGBA', (W, H), BG + (255,)); d = ImageDraw.Draw(img)
    d.text((GAP + 6, 20), 'Posy cursor in context', font=F_TITLE, fill=TEXT)
    d.text((GAP + 6, 56), 'Real mosWM frames (1280\u00d7720), cropped around the pointer and enlarged 2\u00d7 \u2014 the title bar, window corner and desktop states are chosen by the compositor itself; the rest are what an app requests.',
           font=F_S, fill=MUTED)
    for i in range(14):
        r, c = divmod(i, cols)
        x0 = GAP + c * (TW + GAP); y0 = 90 + GAP + r * (TH + GAP)
        d.rounded_rectangle([x0, y0, x0 + TW - 1, y0 + TH - 1], 10, fill=PANEL + (255,), outline=EDGE + (255,))
        full = read_ppm(os.path.join(dump, 'ctx_%02d.ppm' % i))
        cx, cy = AT[i]
        l = min(max(cx - CW // 2, 0), full.width - CW); t = min(max(cy - CH // 2, 0), full.height - CH)
        crop = full.crop((l, t, l + CW, t + CH)).resize((CW * K, CH * K), Image.NEAREST)
        img.paste(crop, (x0 + 12, y0 + 10))
        d.rectangle([x0 + 12, y0 + 10, x0 + 12 + CW * K - 1, y0 + 10 + CH * K - 1], outline=EDGE + (255,))
        d.text((x0 + 14, y0 + CH * K + 18), NAMES[i][0], font=F_H, fill=TEXT)
        cap = NAMES[i][2]
        d.text((x0 + TW - 14 - d.textlength(cap, font=F_S), y0 + CH * K + 22), cap, font=F_S, fill=MUTED)
    img.convert('RGB').save(os.path.join(out, 'posy_cursor_in_context.png'))

# ===================== 3. animated busy / working ==========================
def gif_animated():
    items = [(1, 'Busy'), (2, 'Working')]
    n = len(sprites[1]['frames']); delay = sprites[1]['delay']
    BW, BH = 150, 150
    W, H = 4 * BW + 5 * 14, BH + 90
    frames = []
    for f in range(n):
        img = Image.new('RGBA', (W, H), BG + (255,)); d = ImageDraw.Draw(img)
        d.text((14, 12), 'Posy \u2014 animated cursors', font=F_H, fill=TEXT)
        d.text((14, 36), 'busy.ani + working.ani \u00b7 %d frames @ %d ms (frame %02d/%d)' % (n, delay, f + 1, n), font=F_S, fill=MUTED)
        k = 0
        for st, nm in items:
            sp = sprites[st]
            for kind in ('dark', 'light'):
                x = 14 + k * (BW + 14); y = 64; k += 1
                b = wallpaper_box(BW, BH) if kind == 'dark' else checker(BW, BH)
                place(b, sp, f, BW // 2 - (sp['w'] // 2 - sp['hx']) * 4, BH // 2 - (sp['h'] // 2 - sp['hy']) * 4, 4)
                img.alpha_composite(b, (x, y)); d.rectangle([x, y, x + BW - 1, y + BH - 1], outline=EDGE + (255,))
                d.text((x + 6, y + BH - 16), '%s \u00b7 %s' % (nm, kind), font=F_S, fill=(150, 175, 195) if kind == 'dark' else (80, 95, 105))
        frames.append(img.convert('P', palette=Image.ADAPTIVE, colors=128))
    frames[0].save(os.path.join(out, 'posy_cursor_animated.gif'), save_all=True, append_images=frames[1:],
                   duration=max(20, delay), loop=0, disposal=1)

# ===================== 4. 128px source art =================================
def load_cur(path):
    return cur_bytes(open(path, 'rb').read())

def cur_bytes(data):
    """Largest entry of a .cur blob (32-bit DIB or embedded PNG) -> RGBA image."""
    n = struct.unpack_from('<H', data, 4)[0]
    best = max(range(n), key=lambda k: data[6 + 16 * k] or 256)
    e = 6 + 16 * best; w = data[e] or 256; h = data[e + 1] or 256
    size, off = struct.unpack_from('<II', data, e + 8)
    blob = data[off:off + size]
    if blob[:8] == b'\x89PNG\r\n\x1a\n':
        return Image.open(io.BytesIO(blob)).convert('RGBA')
    hdr = struct.unpack_from('<I', blob, 0)[0]
    return Image.frombuffer('RGBA', (w, h), blob[hdr:hdr + w * h * 4], 'raw', 'BGRA', 0, -1)

def ani_frames(path):
    d = open(path, 'rb').read(); out_frames = []
    i = d.find(b'fram') + 4
    while i + 8 <= len(d):
        cid = d[i:i + 4]; sz = struct.unpack_from('<I', d, i + 4)[0]
        if cid == b'icon':
            out_frames.append(cur_bytes(d[i + 8:i + 8 + sz]))
        i += 8 + sz + (sz & 1)
    return out_frames

def sheet_hires():
    cols, rows = 5, 3
    T, GAP = 236, 14
    W = cols * T + (cols + 1) * GAP; H = 100 + rows * (T + 28) + (rows + 1) * GAP
    img = Image.new('RGBA', (W, H), BG + (255,)); d = ImageDraw.Draw(img)
    d.text((GAP + 6, 20), 'Posy \u2014 source art (128\u00d7128)', font=F_TITLE, fill=TEXT)
    d.text((GAP + 6, 56), 'The pack ships 32/48/64/96/128 px bitmaps; mosWM currently loads the 32 px entry (CURSOR_MAX_SIZE = 32). Shown here at the highest resolution in each file.',
           font=F_S, fill=MUTED)
    for i, (label, fname, _) in enumerate(NAMES):
        r, c = divmod(i, cols); x0 = GAP + c * (T + GAP); y0 = 90 + GAP + r * (T + 28 + GAP)
        d.rounded_rectangle([x0, y0, x0 + T - 1, y0 + T + 27], 10, fill=PANEL + (255,), outline=EDGE + (255,))
        box = wallpaper_box(T - 20, T - 20)
        fr = ani_frames(os.path.join(pack, fname)) if fname.endswith('.ani') else [load_cur(os.path.join(pack, fname))]
        src = fr[len(fr) // 8 if len(fr) > 1 else 0]
        bb = src.getchannel('A').getbbox() or (0, 0, 128, 128)
        src = src.crop(bb)                                  # centre the opaque art in the tile
        k = min(1.5, (T - 60) / max(src.width, src.height))
        s = src.resize((max(1, int(src.width * k)), max(1, int(src.height * k))), Image.LANCZOS)
        box.alpha_composite(s, ((T - 20 - s.width) // 2, (T - 20 - s.height) // 2))
        img.alpha_composite(box, (x0 + 10, y0 + 10))
        d.text((x0 + 14, y0 + T - 6), label, font=F_H, fill=TEXT)
        d.text((x0 + T - 14 - d.textlength(fname, font=F_S), y0 + T - 2), fname, font=F_S, fill=MUTED)
    img.convert('RGB').save(os.path.join(out, 'posy_cursor_source_128.png'))

sheet_states(); sheet_context(); sheet_hires(); gif_animated()
print('cursor showcase written to', out)
