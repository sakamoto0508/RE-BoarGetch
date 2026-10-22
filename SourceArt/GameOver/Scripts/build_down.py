"""Builds Player_GameOver_Down on the existing player skeleton (rig exported from UE, see SourceArt/StageClear/UEExport) with the real net.
Retro-action knockdown: impact recoil (upper body arches back) -> stagger one step back -> knees buckle into a sit (shirimochi)
-> falls onto the back -> small bounce -> holds still.  No root motion (pelvis bone translation only, max ~40 cm back).
Ground contact is solved per frame from the deformed body mesh (no penetration of the floor), the net is laid flat beside the
body, and a per-frame report lists floor/net clearance and net-inside-body vertices.
Run: blender -b --python build_down.py [-- --render]"""
import sys, math
sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/StageClear/Scripts')
import bpy
from mathutils import Matrix, Quaternion, Vector
from mathutils.bvhtree import BVHTree
from victory_common import reset, load_rig, attach_net, assign_action, net_points_world, setup_preview, aim_camera, render

OUT = 'C:/UnrealGames/REBoarGetch/SourceArt/GameOver/'
FPS = 30
DURATION = 1.4
NFRAMES = int(round(DURATION * FPS))
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
A2W = rig.matrix_world
W2A = A2W.inverted()


def upd():
    bpy.context.view_layer.update()


def head(n):
    return rig.pose.bones[n].head.copy()


def rotate_about(n, q, pivot=None):
    pb = rig.pose.bones[n]
    p = pb.head.copy() if pivot is None else pivot
    pb.matrix = Matrix.Translation(p) @ q.to_matrix().to_4x4() @ Matrix.Translation(-p) @ pb.matrix
    upd()


def aim(n, direction):
    pb = rig.pose.bones[n]
    y = (pb.matrix.to_3x3() @ Vector((0, 1, 0))).normalized()
    rotate_about(n, y.rotation_difference(direction.normalized()))


def two_bone(upper, lower, target, hint):
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


def slerp_vec(a, b, t):
    return Quaternion().slerp(a.rotation_difference(b), t) @ a


def ease(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


def track(keys, t):
    """keys: [(time, value)] with value float or Vector; eased (smoothstep) between keys."""
    if t <= keys[0][0]:
        return keys[0][1]
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if t <= t1:
            a = ease((t - t0) / (t1 - t0))
            return v0 + (v1 - v0) * a
    return keys[-1][1]


# ---------------- reference values (idle base pose) ----------------
for pb in rig.pose.bones:
    pb.matrix_basis = base[pb.name]
upd()
foot0 = {s: head('foot_' + s) for s in 'lr'}
footrot0 = {s: rig.pose.bones['foot_' + s].matrix.to_3x3().copy() for s in 'lr'}
pelvis0 = head('pelvis')
grip0, nethead0 = [W2A @ p for p in net_points_world(net)]
net_dir0 = (nethead0 - grip0).normalized()
idle_dirs = {n: (rig.pose.bones[n].matrix.to_3x3() @ Vector((0, 1, 0))).normalized()
             for n in ['upperarm_r', 'lowerarm_r', 'upperarm_l', 'lowerarm_l']}

# Vertex groups -> region masks for floor contact measurement.
FOOT_GROUPS = {'l': {'foot_l', 'ball_l'}, 'r': {'foot_r', 'ball_r'}}
gidx = {g.index: g.name for g in body.vertex_groups}
vert_region = []
for v in body.data.vertices:
    best, w = None, 0.0
    for g in v.groups:
        if g.weight > w:
            best, w = gidx.get(g.group), g.weight
    vert_region.append('l' if best in FOOT_GROUPS['l'] else 'r' if best in FOOT_GROUPS['r'] else 'body')


def body_points():
    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg)
    me = ev.to_mesh()
    M = W2A @ body.matrix_world
    pts = [M @ v.co for v in me.vertices]
    ev.to_mesh_clear()
    return pts


def region_min_z(pts):
    m = {'l': 1e9, 'r': 1e9, 'body': 1e9}
    for p, r in zip(pts, vert_region):
        if p.z < m[r]:
            m[r] = p.z
    return m


def net_min_z():
    M = W2A @ net.matrix_world
    return min((M @ v.co).z for v in net.data.vertices)


FLOOR = min(region_min_z(body_points()).values())
print('FLOOR armature z (cm)', round(FLOOR, 2), 'pelvis0', tuple(round(x, 1) for x in pelvis0))

# ---------------- choreography (armature space, cm / degrees; forward = -Y, character left = +X) ----------------
T_HIT, T_STAGGER, T_STEP, T_BUCKLE, T_SIT, T_TIP, T_IMPACT, T_BOUNCE, T_REST = .0, .2, .4, .56, .74, .86, 1.06, 1.18, 1.32
PELVIS_DY = [(0, 0.), (.12, 5.), (T_STEP, 16.), (T_BUCKLE, 21.), (T_SIT, 30.), (T_IMPACT, 38.), (DURATION, 38.)]
PELVIS_DZ = [(0, 0.), (.12, -2.), (T_STEP, -9.), (T_BUCKLE, -18.), (T_SIT, -60.), (DURATION, -60.)]
PITCH = [(0, 0.), (.12, -13.), (T_STAGGER, -9.), (T_STEP, -4.), (T_BUCKLE, 7.), (T_SIT, -12.), (T_TIP, -30.),
         (T_IMPACT, -88.), (T_BOUNCE, -80.), (T_REST, -87.), (DURATION, -87.)]
ROLL = [(0, 0.), (T_STAGGER, 3.), (T_STEP, -5.), (T_BUCKLE, 2.), (T_SIT, 0.), (DURATION, 0.)]
ARCH = [(0, 0.), (.12, -16.), (T_STAGGER, -10.), (T_STEP, -3.), (T_BUCKLE, 6.), (T_SIT, 2.), (T_IMPACT, -2.), (DURATION, -2.)]
HEADP = [(0, 0.), (.12, -18.), (T_STAGGER, -10.), (T_BUCKLE, 6.), (T_SIT, 10.), (T_TIP, 14.), (T_IMPACT, 4.), (T_BOUNCE, 10.), (T_REST, 6.), (DURATION, 6.)]
HEADYAW = [(0, 0.), (T_IMPACT, 0.), (T_REST, 18.), (DURATION, 18.)]   # KO look: head rolls to the side on the floor
# Feet: offsets from the idle ankle (cm).  Right foot steps back, then both slide forward into a sit and stretch out lying.
FOOT_R = [(0, Vector((0, 0, 0))), (T_STAGGER, Vector((0, 4, 6))), (T_STEP - .06, Vector((-2, 22, 7))), (T_STEP, Vector((-2, 26, 0))),
          (T_BUCKLE, Vector((-2, 24, 0))), (T_SIT, Vector((-6, -8, 0))), (T_TIP, Vector((-8, -14, 4))), (T_IMPACT - .06, Vector((-10, -10, 26))),
          (T_IMPACT + .06, Vector((-12, -24, 0))), (DURATION, Vector((-12, -24, 0)))]
FOOT_L = [(0, Vector((0, 0, 0))), (T_STEP, Vector((2, 6, 0))), (T_BUCKLE, Vector((3, 4, 0))), (T_SIT, Vector((6, -14, 0))),
          (T_TIP, Vector((8, -20, 3))), (T_IMPACT - .04, Vector((12, -16, 30))), (T_IMPACT + .08, Vector((14, -30, 0))), (DURATION, Vector((14, -30, 0)))]
TOES_UP = [(0, 0.), (T_STEP, 0.), (T_SIT, 35.), (T_IMPACT, 70.), (DURATION, 70.)]
KNEE_HINT_UP = [(0, 0.), (T_BUCKLE, .2), (T_SIT, 1.), (T_IMPACT, .6), (DURATION, .6)]  # knees bend up (sit) rather than forward

# Arms: recoil fling -> flail during the stagger -> flop onto the floor beside the body (banzai-ish, palms up).
ARM_KEYS = {
    'upperarm_l': [(0, None), (.12, Vector((.75, -.35, .55))), (T_STEP, Vector((.85, -.1, .2))), (T_SIT, Vector((.8, .1, -.3))),
                   (T_IMPACT, Vector((.85, .45, .1))), (DURATION, Vector((.86, .5, -.06)))],
    'lowerarm_l': [(0, None), (.12, Vector((.55, -.65, .5))), (T_STEP, Vector((.75, -.45, .3))), (T_SIT, Vector((.55, -.3, -.6))),
                   (T_IMPACT, Vector((.55, .8, .15))), (DURATION, Vector((.5, .86, -.05)))],
    'upperarm_r': [(0, None), (.12, Vector((-.75, -.3, .58))), (T_STEP, Vector((-.85, -.05, .25))), (T_SIT, Vector((-.8, .15, -.28))),
                   (T_IMPACT, Vector((-.93, .3, .08))), (DURATION, Vector((-.96, .25, -.06)))],
    'lowerarm_r': [(0, None), (.12, Vector((-.6, -.6, .5))), (T_STEP, Vector((-.8, -.4, .2))), (T_SIT, Vector((-.6, -.2, -.55))),
                   (T_IMPACT, Vector((-.78, .6, .1))), (DURATION, Vector((-.8, .58, -.04)))],
}
# Net shaft direction: carried -> flung up by the recoil -> dropped flat on the floor, pointing out and up-screen from the right hand.
NET_DIR = [(0, None), (.12, Vector((-.45, -.35, .82))), (T_STEP, Vector((-.75, -.25, .6))), (T_SIT, Vector((-.85, -.35, .2))),
           (T_IMPACT, Vector((-.72, .66, .02))), (DURATION, Vector((-.72, .69, 0.)))]


def arm_dir(n, t):
    keys = [(k, v if v is not None else idle_dirs[n]) for k, v in ARM_KEYS[n]]
    return track(keys, t).normalized()


NET_TWIST = 0.0   # solved below so the hoop lies flat on the floor in the final pose


def pose_frame(t, contact=True, twist=None):
    for pb in rig.pose.bones:
        pb.matrix_basis = base[pb.name]
    upd()
    pb = rig.pose.bones['pelvis']
    pb.matrix = Matrix.Translation((0, track(PELVIS_DY, t), track(PELVIS_DZ, t))) @ pb.matrix
    upd()
    piv = head('pelvis')
    rotate_about('pelvis', Quaternion(Vector((0, 1, 0)), math.radians(track(ROLL, t))) @
                 Quaternion(Vector((1, 0, 0)), math.radians(track(PITCH, t))), piv)
    arch = math.radians(track(ARCH, t))
    for n, w in (('spine_01', .3), ('spine_02', .35), ('spine_03', .35)):
        rotate_about(n, Quaternion(Vector((1, 0, 0)), arch * w))
    hp = math.radians(track(HEADP, t))
    rotate_about('neck_01', Quaternion(Vector((1, 0, 0)), hp * .4))
    up_axis = (rig.pose.bones['spine_03'].matrix.to_3x3() @ Vector((0, 1, 0))).normalized()
    rotate_about('head', Quaternion(up_axis, math.radians(track(HEADYAW, t))) @ Quaternion(Vector((1, 0, 0)), hp * .6))
    # legs
    hint_up = track(KNEE_HINT_UP, t)
    toes = math.radians(track(TOES_UP, t))
    for s, keys in (('l', FOOT_L), ('r', FOOT_R)):
        side = 1 if s == 'l' else -1
        target = foot0[s] + track(keys, t)
        hint = (Vector((0, -1, 0)) * (1 - hint_up) + Vector((0, -.35, 1)) * hint_up + Vector((side * .2, 0, 0))).normalized()
        two_bone('thigh_' + s, 'calf_' + s, target, hint)
        set_world_rotation('foot_' + s, Matrix.Rotation(-toes, 3, 'X') @ footrot0[s])
    # arms
    for n in ['upperarm_l', 'lowerarm_l', 'upperarm_r', 'lowerarm_r']:
        aim(n, arm_dir(n, t))
    g, h = [W2A @ p for p in net_points_world(net)]
    want = track([(k, v if v is not None else net_dir0) for k, v in NET_DIR], t).normalized()
    rotate_about('hand_r', (h - g).normalized().rotation_difference(want))
    tw = (NET_TWIST if twist is None else twist) * ease((t - T_SIT) / (T_IMPACT - T_SIT))
    if abs(tw) > 1e-4:
        rotate_about('hand_r', Quaternion(want, tw))
    if not contact:
        return
    # Ground contact: never below the floor; from the sit onward the body rests on it.
    pts = body_points()
    mz = region_min_z(pts)
    lift = FLOOR - mz['body']
    if lift > 0 or t >= T_SIT:
        settle = 1.0 if lift > 0 else ease((t - T_SIT) / .06)
        dz = lift * settle
        pb = rig.pose.bones['pelvis']
        pb.matrix = Matrix.Translation((0, 0, dz)) @ pb.matrix
        upd()
        # re-solve legs with the same targets so the feet keep their contact
        for s, keys in (('l', FOOT_L), ('r', FOOT_R)):
            side = 1 if s == 'l' else -1
            target = foot0[s] + track(keys, t)
            hint = (Vector((0, -1, 0)) * (1 - hint_up) + Vector((0, -.35, 1)) * hint_up + Vector((side * .2, 0, 0))).normalized()
            two_bone('thigh_' + s, 'calf_' + s, target, hint)
            set_world_rotation('foot_' + s, Matrix.Rotation(-toes, 3, 'X') @ footrot0[s])
        pts = body_points()
        mz = region_min_z(pts)
    # feet: lift a foot that would sink into the floor (heels while lying, toes during the stagger)
    for s, keys in (('l', FOOT_L), ('r', FOOT_R)):
        if mz[s] < FLOOR - .3:
            side = 1 if s == 'l' else -1
            target = foot0[s] + track(keys, t) + Vector((0, 0, FLOOR - mz[s]))
            hint = (Vector((0, -1, 0)) * (1 - hint_up) + Vector((0, -.35, 1)) * hint_up + Vector((side * .2, 0, 0))).normalized()
            two_bone('thigh_' + s, 'calf_' + s, target, hint)
            set_world_rotation('foot_' + s, Matrix.Rotation(-toes, 3, 'X') @ footrot0[s])
    # net: lift the shaft until the hoop clears the floor
    for _ in range(12):
        nz = net_min_z()
        if nz >= FLOOR - .2:
            break
        g, h = [W2A @ p for p in net_points_world(net)]
        d = (h - g).normalized()
        axis = d.cross(Vector((0, 0, 1)))
        if axis.length < 1e-4:
            break
        rotate_about('hand_r', Quaternion(axis.normalized(), math.radians(3)))


# ---------------- solve the net twist so the hoop lies flat in the final pose ----------------
best = (1e9, 0.0)
for deg in range(-90, 91, 15):
    pose_frame(DURATION, contact=False, twist=math.radians(deg))
    M = W2A @ net.matrix_world
    zs = [(M @ v.co).z for v in net.data.vertices]
    height = max(zs) - min(zs)
    if height < best[0]:
        best = (height, math.radians(deg))
NET_TWIST = best[1]
print('NET twist (deg)', round(math.degrees(NET_TWIST)), 'hoop height cm', round(best[0], 1))

# ---------------- bake ----------------
act = bpy.data.actions.new('Player_GameOver_Down')
act.use_fake_user = True
rig.animation_data.action = None
assign_action(rig, act)
report = []
for f in range(0, NFRAMES + 1):
    t = f / FPS
    pose_frame(t)
    for pb in rig.pose.bones:
        pb.keyframe_insert('location', frame=f, group=pb.name)
        pb.keyframe_insert('rotation_quaternion', frame=f, group=pb.name)
    pts = body_points()
    mz = region_min_z(pts)
    dg = bpy.context.evaluated_depsgraph_get()
    bvh = BVHTree.FromObject(body, dg)
    inv = body.matrix_world.inverted()
    inside, clearance = 0, 1e9
    for v in net.data.vertices:
        lv = inv @ (net.matrix_world @ v.co)
        loc, nrm, idx, dist = bvh.find_nearest(lv)
        if loc is None:
            continue
        if (lv - loc).dot(nrm) < 0 and dist < .2:
            inside += 1
        clearance = min(clearance, dist)
    report.append((f, t, round(mz['body'] - FLOOR, 1), round(mz['l'] - FLOOR, 1), round(mz['r'] - FLOOR, 1),
                   round(net_min_z() - FLOOR, 1), inside, round(clearance * 100, 1), round(head('pelvis').y - pelvis0.y, 1)))
for fc in getattr(act, 'fcurves', []):
    for k in fc.keyframe_points:
        k.interpolation = 'LINEAR'
scn.frame_start, scn.frame_end = 0, NFRAMES
scn.render.fps, scn.render.fps_base = FPS, 1.0
for r in report:
    print('FRAME f=%d t=%.2f bodyAboveFloor=%.1f footL=%.1f footR=%.1f netAboveFloor=%.1f netVertsInsideBody=%d netClearanceCm=%.1f pelvisBack=%.1f' % r)
bpy.ops.wm.save_as_mainfile(filepath=OUT + 'Player_GameOver_Down.blend')

if DO_RENDER:
    scn.frame_set(0)
    upd()
    floor_w = (A2W @ Vector((0, 0, FLOOR))).z
    cam = setup_preview(floor_w, res=(640, 520))
    views = {'side': ((5.2, .4, floor_w + 1.2), (0, .3, floor_w + .45)),
             'game': ((1.2, -4.2, floor_w + 2.4), (0, .2, floor_w + .4)),
             'top': ((1.6, .9, floor_w + 4.0), (0, .4, floor_w))}
    for name, (loc, tgt) in views.items():
        aim_camera(cam, loc, tgt)
        for f in [0, 4, 8, 12, 17, 22, 26, 30, 33, 36, 42]:
            scn.frame_set(f)
            render(OUT + 'Preview/down_%s_f%02d.png' % (name, f))

bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.ops.export_scene.fbx(filepath=OUT + 'Player_GameOver_Down.fbx', use_selection=True, object_types={'ARMATURE'},
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
                         bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0, use_armature_deform_only=False)
print('EXPORTED', OUT + 'Player_GameOver_Down.fbx')
