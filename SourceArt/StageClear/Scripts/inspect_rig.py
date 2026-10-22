import bpy, sys
root = 'C:/UnrealGames/REBoarGetch/SourceArt/StageClear/UEExport/'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=root + 'A_Player_Idle.fbx')
for o in bpy.data.objects:
    print('OBJ', o.name, o.type, tuple(round(v,4) for v in o.scale), tuple(round(v,3) for v in o.rotation_euler), o.parent.name if o.parent else None)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
for b in arm.data.bones:
    print('BONE', b.name, b.parent.name if b.parent else None, tuple(round(v,2) for v in b.head_local), tuple(round(v,2) for v in b.tail_local))
print('ACTIONS', [(a.name, a.frame_range[:]) for a in bpy.data.actions])
print('FPS', bpy.context.scene.render.fps)
bpy.context.scene.frame_set(0)
for n in ['root','pelvis','hand_r','head','foot_l']:
    pb = arm.pose.bones.get(n)
    if pb:
        print('POSE', n, tuple(round(v,2) for v in (arm.matrix_world @ pb.head)), tuple(round(v,3) for v in pb.location), tuple(round(v,3) for v in pb.rotation_quaternion))
