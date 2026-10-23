"""ボアのUE書き出しFBXをBlenderへ読み込み、骨階層・レスト姿勢・既存Walkの足の軌跡を出力します。
Run: blender -b --python inspect_rig.py"""
import bpy, math
from mathutils import Vector
EXP = 'C:/UnrealGames/REBoarGetch/SourceArt/Boar/UEExport/'
bpy.ops.wm.read_factory_settings(use_empty=True)
def imp(p):
    before = set(bpy.data.objects); bpy.ops.import_scene.fbx(filepath=p); return [o for o in bpy.data.objects if o not in before]
objs = imp(EXP + 'AS_BoarWalk.fbx')
for o in objs: print('OBJ', o.name, o.type, tuple(round(v,4) for v in o.location), tuple(round(math.degrees(v),1) for v in o.rotation_euler), tuple(round(v,4) for v in o.scale), o.parent.name if o.parent else None)
rig = next(o for o in objs if o.type == 'ARMATURE')
def tree(b, d=0):
    print('  '*d + b.name, 'head', tuple(round(v,3) for v in (rig.matrix_world @ b.head_local)), 'tail', tuple(round(v,3) for v in (rig.matrix_world @ b.tail_local)), 'len', round(b.length,3))
    for c in b.children: tree(c, d+1)
for b in rig.data.bones:
    if b.parent is None: tree(b)
act = rig.animation_data.action
print('ACTION', act.name, act.frame_range[:], 'fps', bpy.context.scene.render.fps)
scn = bpy.context.scene
feet = ['frontleg2', 'R_frontleg2', 'backleg2', 'R_backleg2']
for f in range(int(act.frame_range[0]), int(act.frame_range[1]) + 1):
    scn.frame_set(f)
    row = []
    for n in feet:
        pb = rig.pose.bones[n]
        p = rig.matrix_world @ pb.tail
        row.append('%s=(%.3f,%.3f,%.3f)' % (n, p.x, p.y, p.z))
    h = rig.matrix_world @ rig.location
    hd = rig.matrix_world @ rig.pose.bones['head'].head
    print('F%02d' % f, ' '.join(row), 'hips=(%.3f,%.3f,%.3f)' % tuple(h), 'head=(%.3f,%.3f,%.3f)' % tuple(hd))
print('FCURVES')
try:
    fcs = act.fcurves
except AttributeError:
    fcs = [fc for l in act.layers for s in l.strips for cb in s.channelbags for fc in cb.fcurves]
for fc in fcs:
    vals = [kp.co[1] for kp in fc.keyframe_points]
    print(' ', fc.data_path, fc.array_index, 'n', len(vals), 'min %.4f max %.4f' % (min(vals), max(vals)))
print('POSEBONE rest local axes (world Y axis of each bone):')
for b in rig.data.bones:
    m = rig.matrix_world.to_3x3() @ b.matrix_local.to_3x3()
    print(' ', b.name, 'X', tuple(round(v,2) for v in m.col[0].normalized()), 'Y', tuple(round(v,2) for v in m.col[1].normalized()), 'Z', tuple(round(v,2) for v in m.col[2].normalized()))
