import sys
sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/StageClear/Scripts')
import bpy
from mathutils import Vector
from victory_common import *

reset()
rig, body, idle = load_rig()
net = attach_net(rig)
assign_action(rig, idle)
bpy.context.scene.frame_set(1)
bpy.context.view_layer.update()
g, h = net_points_world(net)
print('NET grip', tuple(round(x, 3) for x in g), 'head', tuple(round(x, 3) for x in h))
for n in ['hand_r', 'pelvis', 'ball_l', 'head']:
    print('BONE', n, tuple(round(x, 3) for x in (rig.matrix_world @ rig.pose.bones[n].head)))
dg = bpy.context.evaluated_depsgraph_get()
ev = body.evaluated_get(dg)
zs = [(body.matrix_world @ v.co).z for v in ev.to_mesh().vertices]
print('BODY z', min(zs), max(zs))
floor = min(zs)
cam = setup_preview(floor)
aim_camera(cam, (1.4, -3.4, floor + 1.1), (0, 0, floor + 0.85)); render(ROOT + 'Preview/check_idle_front.png')
aim_camera(cam, (3.6, -0.4, floor + 1.1), (0, 0, floor + 0.85)); render(ROOT + 'Preview/check_idle_side.png')
