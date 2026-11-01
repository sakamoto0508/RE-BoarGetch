"""Inspect rig bones, the ground NetSwing grip and FallLoop pose (renders side previews)."""
import sys
sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/StageClear/Scripts')
import bpy
from mathutils import Vector
from victory_common import *
AIR = 'C:/UnrealGames/REBoarGetch/SourceArt/AirNetDrop/'
reset()
rig, body, idle = load_rig()
net = attach_net(rig)
swing = load_action(EXP + 'A_Player_NetSwing.fbx', 'A_Player_NetSwing')
fall = load_action(AIR + 'UEExport/A_Player_FallLoop.fbx', 'A_Player_FallLoop')
for b in rig.data.bones:
    print('BONE', b.name, b.parent.name if b.parent else None, round(b.length, 1))
print('RIG', rig.matrix_world)
W2A = rig.matrix_world.inverted()
scn = bpy.context.scene
assign_action(rig, idle); scn.frame_set(1); bpy.context.view_layer.update()
floor = min((rig.matrix_world @ rig.pose.bones[n].head).z for n in ('ball_l', 'ball_r')) - .005
cam = setup_preview(floor, res=(480, 600))
def dump(tag):
    g, h = net_points_world(net)
    ga, ha = W2A @ g, W2A @ h
    print(tag, 'grip', tuple(round(x, 1) for x in ga), 'head', tuple(round(x, 1) for x in ha),
          'hand_l', tuple(round(x, 1) for x in rig.pose.bones['hand_l'].head), 'hand_r', tuple(round(x, 1) for x in rig.pose.bones['hand_r'].head),
          'pelvis', tuple(round(x, 1) for x in rig.pose.bones['pelvis'].head))
dump('IDLE')
for act, frames in ((swing, [0, 8, 14, 18, 22, 28, 36, 44]), (fall, [0, 20, 40])):
    assign_action(rig, act)
    print('ACTION', act.name, act.frame_range[:])
    for f in frames:
        scn.frame_set(f); bpy.context.view_layer.update()
        dump('%s f%d' % (act.name, f))
        for view, loc in (('side', (5.0, -.5, floor + 1.3)), ('front', (1.6, -4.6, floor + 1.4))):
            aim_camera(cam, loc, (0, -.3, floor + .95))
            render(AIR + 'Preview/inspect/%s_%s_f%02d.png' % (act.name, view, f))
