"""Stitch Before/After captures side by side (Blender background python, no UE).
blender -b --factory-startup --python make_compare_sheet.py
Writes Screenshots/compare/compare_<view>.png (left: before, right: after) and compare_all.png (4 rows, half size)."""
import bpy, os
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SH = os.path.join(ROOT, 'Screenshots')
OUT = os.path.join(SH, 'compare')
os.makedirs(OUT, exist_ok=True)
VIEWS = ['top', 'start_view', 'cage_view', 'ridge_view', 'oblique']


def load(p):
    im = bpy.data.images.load(p)
    w, h = im.size
    a = np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)
    return a


def save(a, p):
    h, w = a.shape[:2]
    im = bpy.data.images.new(os.path.basename(p), w, h, alpha=True)
    im.pixels = a.ravel().tolist()
    im.filepath_raw = p
    im.file_format = 'PNG'
    im.save()


rows = []
for v in VIEWS:
    b = load(os.path.join(SH, 'before', v + '.png'))
    a = load(os.path.join(SH, 'after', v + '.png'))
    gap = np.ones((b.shape[0], 16, 4), dtype=np.float32)
    row = np.concatenate([b, gap, a], axis=1)
    save(row, os.path.join(OUT, 'compare_%s.png' % v))
    rows.append(row[::2, ::2])
sep = np.ones((12, rows[0].shape[1], 4), dtype=np.float32)
allimg = rows[0]
for r in rows[1:4]:
    allimg = np.concatenate([r, sep, allimg], axis=0)   # Blender images are bottom-up: prepend = lower rows
save(allimg, os.path.join(OUT, 'compare_all.png'))
print('[S02] compare sheets written')
