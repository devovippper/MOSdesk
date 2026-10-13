import sys
from PIL import Image, ImageDraw
sys.path.insert(0, '.')
import icons
scale = int(sys.argv[2]) if len(sys.argv) > 2 else 4
cols = 4
rows = (len(icons.ICONS) + cols - 1) // cols
bg = (46, 66, 88, 255)
sheet = Image.new('RGBA', (cols * 64 * scale + 40, rows * 64 * scale + 40), bg)
for i, (n, fn) in enumerate(icons.ICONS):
    im = fn().img.resize((64 * scale, 64 * scale), Image.NEAREST)
    sheet.alpha_composite(im, (10 + (i % cols) * (64 * scale + 6), 10 + (i // cols) * (64 * scale + 6)))
sheet.convert('RGB').save(sys.argv[1])
