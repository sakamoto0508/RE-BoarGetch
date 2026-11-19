"""2x2 sheet of After shots (Blender background python). blender -b --python make_grid_sheet.py -- out.png a.png b.png c.png d.png"""
import bpy, sys
import numpy as np
args = sys.argv[sys.argv.index('--') + 1:]
out, names = args[0], args[1:5]
def load(p):
    im = bpy.data.images.load(p); w, h = im.size
    return np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)[::2, ::2]
ims = [load(n) for n in names]
gap_v = np.ones((ims[0].shape[0], 12, 4), dtype=np.float32)
top = np.concatenate([ims[0], gap_v, ims[1]], axis=1)
bot = np.concatenate([ims[2], gap_v, ims[3]], axis=1)
sheet = np.concatenate([bot, np.ones((12, top.shape[1], 4), dtype=np.float32), top], axis=0)   # bottom-up storage
h, w = sheet.shape[:2]
im = bpy.data.images.new('sheet', w, h, alpha=True); im.pixels = sheet.ravel().tolist()
im.filepath_raw = out; im.file_format = 'PNG'; im.save()
print('[S02] sheet', out)
