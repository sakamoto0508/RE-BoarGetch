"""Tiles preview PNGs into one contact sheet (Blender -b --python make_sheet.py -- <out.png> <cols> <in1.png> <in2.png> ...)."""
import sys
import bpy
import numpy as np

args = sys.argv[sys.argv.index('--') + 1:]
out, cols, files = args[0], int(args[1]), args[2:]
imgs = []
for f in files:
    im = bpy.data.images.load(f)
    w, h = im.size
    px = np.empty(w * h * 4, dtype=np.float32)
    im.pixels.foreach_get(px)
    imgs.append(px.reshape(h, w, 4))
h, w = imgs[0].shape[:2]
rows = (len(imgs) + cols - 1) // cols
sheet = np.ones((rows * h, cols * w, 4), dtype=np.float32)
for i, px in enumerate(imgs):
    r, c = divmod(i, cols)
    # Blender pixel rows start at the bottom
    sheet[(rows - 1 - r) * h:(rows - r) * h, c * w:(c + 1) * w] = px
img = bpy.data.images.new('sheet', cols * w, rows * h, alpha=True)
img.pixels.foreach_set(sheet.ravel())
img.filepath_raw = out
img.file_format = 'PNG'
img.save()
print('SHEET', out)
