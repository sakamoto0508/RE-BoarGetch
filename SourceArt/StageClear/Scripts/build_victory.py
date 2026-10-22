"""Builds Player_Victory_NetRaise on the existing player skeleton (rig from UE export) with the real net attached.
Small hop (pelvis bone translation, no root motion) + raise the net high, then hold.  Saves .blend + .fbx + previews.
Run: blender -b --python build_victory.py [-- --render]"""
import sys, math
sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/StageClear/Scripts')
import bpy
from mathutils import Matrix, Quaternion, Vector
from mathutils.bvhtree import BVHTree
from victory_common import *

FPS = 30
DURATION = 1.9
NFRAMES = int(round(DURATION * FPS))          # last frame index
T_TAKEOFF, T_APEX, T_LAND = .27, .54, .80
JUMP_HEIGHT = 30.0                            # cm, pelvis rise at apex
DO_RENDER = '--render' in sys.argv

reset()
rig, body, idle = load_rig()
net = attach_net(rig)
assign_action(rig, idle)
scn = bpy.context.scene
scn.frame_set(1)
bpy.context.view_layer.update()
base = {pb.name: pb.matrix_basis.copy() for pb in rig.pose.bones}
for pb in rig.pose.bones:
    pb.rotation_mode = 'QUATERNION'

# Armature space == world axes in cm (rig object scale 0.01, no rotation).
A2W = rig.matrix_world
W2A = A2W.inverted()


def upd():
    bpy.context.view_layer.update()


def smooth(a, b, t):
    t = min(1.0, max(0.0, (t - a) / (b - a))) if b > a else 1.0
    return t * t * (3 - 2 * t)


def head(n):
    return rig.pose.bones[n].head.copy()


def rotate_about(n, q, pivot=None):
    pb = rig.pose.bones[n]
    p = pb.head.copy() if pivot is None else pivot
    pb.matrix = Matrix.Translation(p) @ q.to_matrix().to_4x4() @ Matrix.Translation(-p) @ pb.matrix
    upd()


def aim(n, direction):
    """Rotate bone (minimal arc) so its Y axis points along direction (armature space)."""
    pb = rig.pose.bones[n]
    y = (pb.matrix.to_3x3() @ Vector((0, 1, 0))).normalized()
    rotate_about(n, y.rotation_difference(direction.normalized()))


def two_bone(upper, lower, target, hint):
    """Analytic 2-bone IK: aim upper at the joint and lower at target; joint bends toward hint."""
    h = head(upper)
    l1 = (head(lower) - h).length
    l2 = rig.pose.bones[lower].length
    d = target - h
    dist = min(max(d.length, 1e-3), (l1 + l2) * .999)
    u = d.normalized()
    v = (hint - u * hint.dot(u))
    v = v.normalized() if v.length > 1e-4 else Vector((0, -1, 0))
    cos_a = max(-1.0, min(1.0, (l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist)))
    a = math.acos(cos_a)
    joint = h + l1 * (math.cos(a) * u + math.sin(a) * v)
    aim(upper, joint - h)
    aim(lower, h + u * dist - head(lower))


def set_world_rotation(n, rot3):
    pb = rig.pose.bones[n]
    pb.matrix = Matrix.Translation(pb.head) @ rot3.to_4x4()
    upd()


# ---------------- reference values from the idle base pose ----------------
for pb in rig.pose.bones:
    pb.matrix_basis = base[pb.name]
upd()
foot0 = {s: head('foot_' + s) for s in 'lr'}
footrot0 = {s: rig.pose.bones['foot_' + s].matrix.to_3x3().copy() for s in 'lr'}
grip0, nethead0 = [W2A @ p for p in net_points_world(net)]
net_dir0 = (nethead0 - grip0).normalized()
print('IDLE net dir', tuple(round(x, 3) for x in net_dir0))

FWD = Vector((0, -1, 0))  # character forward in rig space


def pelvis_dz(t):
    if t < .20:
        return -9.0 * smooth(0, .20, t)
    if t < T_TAKEOFF:
        return -9.0 + 11.0 * smooth(.20, T_TAKEOFF, t)
    if t < T_LAND:
        # ballistic arc from +2 at takeoff to +2 at landing, peak JUMP_HEIGHT at T_APEX
        s = (t - T_APEX) / (T_LAND - T_APEX if t > T_APEX else T_APEX - T_TAKEOFF)
        return 2.0 + (JUMP_HEIGHT - 2.0) * (1 - s * s)
    if t < .92:
        return 2.0 - 9.0 * smooth(T_LAND, .92, t)
    if t < 1.18:
        return -7.0 + 7.0 * smooth(.92, 1.18, t)
    return -.8 * math.sin((t - 1.18) * 2 * math.pi / 1.4)  # breathing hold


def foot_lift(t):
    """Feet height above ground (cm). Toes push before takeoff; tucked in the air."""
    if t < T_TAKEOFF or t >= T_LAND:
        return 0.0
    air = pelvis_dz(t) - 2.0
    tuck = 9.0 * math.sin(math.pi * (t - T_TAKEOFF) / (T_LAND - T_TAKEOFF))
    return max(0.0, air - tuck + 2.0)


def raise_amount(t):
    return smooth(.08, .50, t)


D_NET_UP = Vector((-.62, .06, 1.0)).normalized()   # up and outward to the character's right so the shaft clears the head
R_UPPER = Vector((-.70, .04, .71)).normalized()
R_LOWER = Vector((-.42, -.02, .91)).normalized()
L_UPPER = Vector((.78, -.28, .56)).normalized()
L_LOWER = Vector((.25, -.42, .87)).normalized()


def pose_frame(t):
    for pb in rig.pose.bones:
        pb.matrix_basis = base[pb.name]
    upd()
    dz = pelvis_dz(t)
    pb = rig.pose.bones['pelvis']
    pb.matrix = Matrix.Translation((0, 0, dz)) @ pb.matrix
    upd()
    crouch = max(0.0, -dz) / 9.0 if t < T_TAKEOFF + .05 or t > T_LAND else 0.0
    lean = math.radians(10) * crouch - math.radians(7) * smooth(T_TAKEOFF, T_APEX, t) * (1 - smooth(1.0, 1.4, t)) - math.radians(2) * smooth(1.0, 1.4, t)
    for n, w in (('spine_01', .3), ('spine_02', .35), ('spine_03', .35)):
        rotate_about(n, Quaternion(Vector((1, 0, 0)), lean * w))
    # legs: planted feet, lifted/tucked in air
    lift = foot_lift(t)
    for s, side in (('l', 1), ('r', -1)):
        target = foot0[s] + Vector((0, 0, lift))
        hint = (FWD + Vector((side * .15, 0, 0))).normalized()
        two_bone('thigh_' + s, 'calf_' + s, target, hint)
        toe = 0.0
        if T_TAKEOFF - .06 < t < T_LAND:
            toe = math.radians(28) * math.sin(math.pi * min(1, (t - (T_TAKEOFF - .06)) / (T_LAND - T_TAKEOFF + .06)))
        set_world_rotation('foot_' + s, Matrix.Rotation(-toe, 3, 'X') @ footrot0[s])
    # arms
    r = raise_amount(t)
    wave = math.radians(4) * math.sin((t - 1.1) * 2 * math.pi / .9) * smooth(1.05, 1.3, t)
    up_r = slerp_vec(idle_dir('upperarm_r'), R_UPPER, r)
    aim('upperarm_r', up_r)
    aim('lowerarm_r', slerp_vec(idle_dir('lowerarm_r'), R_LOWER, r))
    l = smooth(.12, .48, t)
    aim('upperarm_l', slerp_vec(idle_dir('upperarm_l'), L_UPPER, l))
    aim('lowerarm_l', slerp_vec(idle_dir('lowerarm_l'), L_LOWER, l))
    # wrist: point the net shaft up (blend from the idle carrying angle)
    g, h = [W2A @ p for p in net_points_world(net)]
    cur = (h - g).normalized()
    want = slerp_vec(net_dir0, Quaternion(Vector((0, 1, 0)), wave) @ D_NET_UP, r)
    rotate_about('hand_r', cur.rotation_difference(want))
    # head looks up a little at the apex and stays proud
    rotate_about('head', Quaternion(Vector((1, 0, 0)), -math.radians(9) * smooth(.3, .6, t) + math.radians(3) * smooth(1.0, 1.5, t)))


_idle_dirs = {}


def idle_dir(n):
    return _idle_dirs[n]


def slerp_vec(a, b, t):
    q = a.rotation_difference(b)
    return Quaternion().slerp(q, t) @ a



for n in ['upperarm_r', 'lowerarm_r', 'upperarm_l', 'lowerarm_l']:
    _idle_dirs[n] = (rig.pose.bones[n].matrix.to_3x3() @ Vector((0, 1, 0))).normalized()

# ---------------- bake ----------------
act = bpy.data.actions.new('Player_Victory_NetRaise')
act.use_fake_user = True
rig.animation_data.action = None
assign_action(rig, act)
keyed = [pb for pb in rig.pose.bones]
report = []
body_bvh = None
for f in range(0, NFRAMES + 1):
    t = f / FPS
    pose_frame(t)
    for pb in keyed:
        pb.keyframe_insert('location', frame=f, group=pb.name)
        pb.keyframe_insert('rotation_quaternion', frame=f, group=pb.name)
    if f % 3 == 0:
        dg = bpy.context.evaluated_depsgraph_get()
        bm_body = BVHTree.FromObject(body, dg)
        ev = net.evaluated_get(dg)
        # Net vs deformed body: count net vertices inside the body (normal test) and the closest clearance (cm).
        inside, clearance = 0, 1e9
        inv = body.matrix_world.inverted()
        for v in net.data.vertices:
            lv = inv @ (net.matrix_world @ v.co)
            loc, nrm, idx, dist = bm_body.find_nearest(lv)
            if loc is None:
                continue
            if (lv - loc).dot(nrm) < 0 and dist < .2:
                inside += 1
            clearance = min(clearance, dist)
        g, h = net_points_world(net)
        report.append((f, round(t, 2), round(pelvis_dz(t), 1), inside, round(clearance * 100, 1), tuple(round(x, 2) for x in h)))
for fc in act.fcurves if hasattr(act, 'fcurves') else []:
    for k in fc.keyframe_points:
        k.interpolation = 'LINEAR'
scn.frame_start, scn.frame_end = 0, NFRAMES
scn.render.fps, scn.render.fps_base = FPS, 1.0  # FBX import of the idle clip can reset the scene rate
for r in report:
    print('FRAME f=%d t=%.2f dz=%.1f netVertsInsideBody=%d minClearanceCm=%.1f netHead=%s' % r)

bpy.ops.wm.save_as_mainfile(filepath=ROOT + 'Player_Victory_NetRaise.blend')

if DO_RENDER:
    floor = min((body.matrix_world @ v.co).z for v in body.evaluated_get(bpy.context.evaluated_depsgraph_get()).to_mesh().vertices)
    scn.frame_set(0); upd()
    floor = min((A2W @ head('ball_l')).z, (A2W @ head('ball_r')).z) - .005
    cam = setup_preview(floor, res=(560, 700))
    views = {'front': ((2.2, -6.4, floor + 1.7), (0, 0, floor + 1.55)), 'side': ((6.6, -.6, floor + 1.7), (0, 0, floor + 1.55)),
             'game': ((2.6, -5.0, floor + 1.4), (0, 0, floor + 1.4))}
    for name, (loc, tgt) in views.items():
        aim_camera(cam, loc, tgt)
        for f in [0, 6, 10, 16, 20, 24, 28, 36, 50]:
            scn.frame_set(f)
            render(ROOT + 'Preview/victory_%s_f%02d.png' % (name, f))

# ---------------- export FBX (armature + animation only) ----------------
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.ops.export_scene.fbx(filepath=ROOT + 'Player_Victory_NetRaise.fbx', use_selection=True, object_types={'ARMATURE'},
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
                         bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0, use_armature_deform_only=False)
print('EXPORTED', ROOT + 'Player_Victory_NetRaise.fbx')
