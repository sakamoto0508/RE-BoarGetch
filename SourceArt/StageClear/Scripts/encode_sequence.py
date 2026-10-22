"""Encode PNG screenshots (sorted by name, filtered by prefixes) into an H.264 MP4 using Blender's bundled FFmpeg.
blender -b --python encode_sequence.py -- <folder> <out.mp4> <fps> <prefix> [<prefix> ...]
Each screenshot is held for one frame at <fps> (screenshots were taken ~0.2 s apart -> fps 5 is real time)."""
import bpy, os, sys
args = sys.argv[sys.argv.index('--') + 1:]
folder, out, fps, prefixes = args[0], args[1], int(args[2]), args[3:]
files = sorted(f for f in os.listdir(folder) if f.endswith('.png') and any(f.startswith(p) for p in prefixes))
assert files, 'no screenshots'
img = bpy.data.images.load(os.path.join(folder, files[0]))
w, h = img.size
scn = bpy.context.scene
seq = scn.sequence_editor_create()
strips = seq.strips if hasattr(seq, 'strips') else seq.sequences
strip = strips.new_image(name='shots', filepath=os.path.join(folder, files[0]), channel=1, frame_start=1)
for f in files[1:]:
    strip.elements.append(f)
scn.frame_start, scn.frame_end = 1, len(files)
scn.render.fps = fps
scn.render.resolution_x, scn.render.resolution_y = w - (w % 2), h - (h % 2)
scn.render.resolution_percentage = 100
if hasattr(scn.render.image_settings, 'media_type'):
    scn.render.image_settings.media_type = 'VIDEO'  # Blender 5.x
scn.render.image_settings.file_format = 'FFMPEG'
scn.render.ffmpeg.format = 'MPEG4'
scn.render.ffmpeg.codec = 'H264'
scn.render.ffmpeg.constant_rate_factor = 'HIGH'
scn.render.filepath = out
bpy.ops.render.render(animation=True)
print('ENCODED', out, len(files), 'frames', w, h)
