"""AKI-163: builds Player_HitKnockdown on the existing player skeleton (same rig / helpers as GameOver/Scripts/build_down.py,
rig exported from UE, see SourceArt/StageClear/UEExport) with the real net in the right hand.
Red Boar heavy hit: impact recoil (upper body snaps back) -> stagger one step back -> knees buckle into a sit (shirimochi) on the
floor with a small bounce -> dazed head wobble -> feet tucked in, lean forward, push up -> stand (last frame == idle pose).
No root motion (pelvis bone translation only, max ~22 cm back; the capsule knockback slide is done by gameplay).
Ground contact is solved per frame from the deformed body mesh (no penetration of the floor) and a per-frame report lists
floor / net clearance.  The gameplay StunDuration (default 1.6 s) plays it at rate 1.0; other durations rescale it.
Run: "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --python build_hit_knockdown.py [-- --render]"""
import sys, math
sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/StageClear/Scripts')
import bpy
from mathutils import Matrix, Quaternion, Vector
from victory_common import reset, load_rig, attach_net, assign_action, net_points_world, setup_preview, aim_camera, render

OUT = 'C:/UnrealGames/REBoarGetch/SourceArt/HitReaction/'
FPS = 30
DURATION = 1.6
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
T_RECOIL, T_STEP, T_BUCKLE, T_SIT, T_SETTLE = .10, .30, .42, .56, .70
T_DAZE_END, T_TUCK, T_CROUCH, T_STAND = 1.00, 1.14, 1.30, 1.48
V0 = Vector((0, 0, 0))
PELVIS_DY = [(0, 0.), (T_RECOIL, 4.), (T_STEP, 12.), (T_BUCKLE, 16.), (T_SIT, 22.), (T_DAZE_END, 22.), (T_TUCK, 20.),
             (T_CROUCH, 10.), (T_STAND, 2.), (DURATION, 0.)]
PELVIS_DZ = [(0, 0.), (T_RECOIL, -2.), (T_STEP, -8.), (T_BUCKLE, -20.), (T_SIT, -62.), (T_TUCK, -62.), (T_CROUCH, -36.),
             (T_STAND, -3.), (DURATION, 0.)]
PITCH = [(0, 0.), (T_RECOIL, -12.), (T_STEP, -7.), (T_BUCKLE, 4.), (T_SIT, -10.), (.62, -14.), (T_SETTLE, -9.), (T_DAZE_END, -9.),
         (T_TUCK, 6.), (T_CROUCH, 24.), (T_STAND, 5.), (DURATION, 0.)]
ROLL = [(0, 0.), (T_RECOIL, 2.), (T_STEP, -4.), (T_BUCKLE, 2.), (T_SIT, 0.), (.80, 3.), (.92, -3.), (T_DAZE_END, 0.), (DURATION, 0.)]
ARCH = [(0, 0.), (T_RECOIL, -16.), (T_STEP, -8.), (T_BUCKLE, 5.), (T_SIT, 6.), (T_DAZE_END, 8.), (T_TUCK, 10.), (T_CROUCH, 14.),
        (T_STAND, 2.), (DURATION, 0.)]
HEADP = [(0, 0.), (T_RECOIL, -20.), (T_STEP, -8.), (T_BUCKLE, 6.), (T_SIT, 12.), (.62, 2.), (T_SETTLE, 10.), (T_DAZE_END, 8.),
         (T_CROUCH, -6.), (T_STAND, 0.), (DURATION, 0.)]
# Feet: offsets from the idle ankle (cm).  Right foot steps back, both slide forward into the sit, then are tucked under the body
# and step back to the idle stance while standing up (each foot lifts when it moves, so it never slides while loaded).
FOOT_R = [(0, V0), (.08, Vector((0, 4, 6))), (T_STEP - .06, Vector((-2, 20, 7))), (T_STEP, Vector((-2, 24, 0))),
          (T_BUCKLE, Vector((-2, 22, 0))), (T_SIT, Vector((-6, -10, 0))), (T_DAZE_END, Vector((-6, -10, 0))),
          (T_DAZE_END + .07, Vector((-5, -2, 4))), (T_TUCK, Vector((-4, 6, 0))), (T_CROUCH, Vector((-3, 8, 0))),
          (T_CROUCH + .09, Vector((-2, 4, 7))), (T_STAND - .05, V0), (DURATION, V0)]
FOOT_L = [(0, V0), (T_STEP, Vector((2, 6, 0))), (T_BUCKLE, Vector((3, 4, 0))), (T_SIT, Vector((6, -16, 0))),
          (T_DAZE_END, Vector((6, -16, 0))), (T_DAZE_END + .08, Vector((5, -6, 4))), (T_TUCK + .02, Vector((4, 2, 0))),
          (T_CROUCH, Vector((3, 6, 0))), (T_STAND - .07, Vector((1, 3, 6))), (T_STAND, V0), (DURATION, V0)]
TOES_UP = [(0, 0.), (T_STEP, 0.), (T_SIT, 30.), (T_DAZE_END, 30.), (T_TUCK, 0.), (DURATION, 0.)]
KNEE_HINT_UP = [(0, 0.), (T_BUCKLE, .2), (T_SIT, 1.), (T_TUCK, 1.), (T_CROUCH, .5), (T_STAND, 0.), (DURATION, 0.)]
BOUNCE_CM = 5.0
# Arms: recoil fling -> the left hand is planted behind on the floor (sit support), the right hand keeps the net up out of the dirt
# -> both push off while standing up -> back to the idle carry (None = idle direction).
ARM_KEYS = {
    'upperarm_l': [(0, None), (T_RECOIL, Vector((.75, -.35, .55))), (T_STEP, Vector((.8, -.1, .3))), (T_SIT, Vector((.45, .55, -.7))),
                   (T_TUCK, Vector((.45, .45, -.75))), (T_CROUCH, Vector((.35, -.3, -.88))), (T_STAND, None), (DURATION, None)],
    'lowerarm_l': [(0, None), (T_RECOIL, Vector((.55, -.65, .5))), (T_STEP, Vector((.7, -.45, .3))), (T_SIT, Vector((.25, .4, -.88))),
                   (T_TUCK, Vector((.25, .35, -.9))), (T_CROUCH, Vector((.2, -.4, -.9))), (T_STAND, None), (DURATION, None)],
    'upperarm_r': [(0, None), (T_RECOIL, Vector((-.75, -.3, .58))), (T_STEP, Vector((-.85, -.05, .3))), (T_SIT, Vector((-.75, .15, -.6))),
                   (T_DAZE_END, Vector((-.75, .15, -.6))), (T_CROUCH, Vector((-.6, -.2, -.75))), (T_STAND, None), (DURATION, None)],
    'lowerarm_r': [(0, None), (T_RECOIL, Vector((-.6, -.6, .5))), (T_STEP, Vector((-.8, -.4, .25))), (T_SIT, Vector((-.55, -.45, -.45))),
                   (T_DAZE_END, Vector((-.55, -.45, -.45))), (T_CROUCH, Vector((-.4, -.6, -.6))), (T_STAND, None), (DURATION, None)],
}
NET_DIR = [(0, None), (T_RECOIL, Vector((-.45, -.35, .82))), (T_STEP, Vector((-.7, -.25, .65))), (T_SIT, Vector((-.75, -.45, .48))),
           (T_DAZE_END, Vector((-.75, -.45, .48))), (T_CROUCH, Vector((-.55, -.55, .6))), (T_STAND, None), (DURATION, None)]


def keys_or_idle(keys, idle_value):
    return [(k, v if v is not None else idle_value) for k, v in keys]


def arm_dir(n, t):
    return track(keys_or_idle(ARM_KEYS[n], idle_dirs[n]), t).normalized()


def window(t, a, b, ramp=.08):
    return ease((t - a) / ramp) * (1 - ease((t - (b - ramp)) / ramp))


def bounce(t):
    if T_SIT <= t <= T_SETTLE:
        return BOUNCE_CM * math.sin(math.pi * (t - T_SIT) / (T_SETTLE - T_SIT))
    return 0.0


def solve_legs(t, hint_up, toes, extra=None):
    for s, keys in (('l', FOOT_L), ('r', FOOT_R)):
        side = 1 if s == 'l' else -1
        target = foot0[s] + track(keys, t) + (extra.get(s, V0) if extra else V0)
        hint = (Vector((0, -1, 0)) * (1 - hint_up) + Vector((0, -.35, 1)) * hint_up + Vector((side * .2, 0, 0))).normalized()
        two_bone('thigh_' + s, 'calf_' + s, target, hint)
        set_world_rotation('foot_' + s, Matrix.Rotation(-toes, 3, 'X') @ footrot0[s])


def pose_frame(t, contact=True):
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
    # dazed: the head circles (yaw + roll) while sitting
    daze = window(t, T_SETTLE - .04, T_DAZE_END + .1, .12)
    ph = 2 * math.pi * 2.4 * (t - T_SETTLE)
    up_axis = (rig.pose.bones['spine_03'].matrix.to_3x3() @ Vector((0, 1, 0))).normalized()
    fwd_axis = (rig.pose.bones['spine_03'].matrix.to_3x3() @ Vector((0, 0, 1))).normalized()
    rotate_about('head', Quaternion(up_axis, math.radians(16 * math.sin(ph) * daze)) @
                 Quaternion(fwd_axis, math.radians(11 * math.cos(ph) * daze)) @ Quaternion(Vector((1, 0, 0)), hp * .6))
    hint_up = track(KNEE_HINT_UP, t)
    toes = math.radians(track(TOES_UP, t))
    solve_legs(t, hint_up, toes)
    for n in ['upperarm_l', 'lowerarm_l', 'upperarm_r', 'lowerarm_r']:
        aim(n, arm_dir(n, t))
    g, h = [W2A @ p for p in net_points_world(net)]
    want = track(keys_or_idle(NET_DIR, net_dir0), t).normalized()
    rotate_about('hand_r', (h - g).normalized().rotation_difference(want))
    if not contact:
        return
    # Ground contact: never below the floor; while sitting (T_SIT..T_TUCK) the body rests on it (+ the small bounce).
    pts = body_points()
    mz = region_min_z(pts)
    lift = FLOOR - mz['body']
    resting = T_SIT <= t <= T_TUCK
    if lift > 0 or resting:
        settle = 1.0 if lift > 0 and not resting else ease((t - T_SIT) / .05) if resting else 1.0
        dz = lift * settle + bounce(t)
        pb = rig.pose.bones['pelvis']
        pb.matrix = Matrix.Translation((0, 0, dz)) @ pb.matrix
        upd()
        solve_legs(t, hint_up, toes)
        pts = body_points()
        mz = region_min_z(pts)
    # feet: lift a foot that would sink into the floor
    extra = {s: Vector((0, 0, FLOOR - mz[s])) for s in 'lr' if mz[s] < FLOOR - .3}
    if extra:
        solve_legs(t, hint_up, toes, extra)
    # net: the shaft butt sits below the hand, so lifting the hoop can push the butt into the floor. Greedily tilt the net
    # (about the horizontal axis across the shaft) in whichever direction raises its lowest vertex, until it clears the floor.
    for _ in range(60):
        cur = net_min_z()
        if cur >= FLOOR + 1.0:
            break
        g, h = [W2A @ p for p in net_points_world(net)]
        d = (h - g).normalized()
        axis = d.cross(Vector((0, 0, 1)))
        axis = axis.normalized() if axis.length > 1e-3 else Vector((1, 0, 0))
        best = None
        for sgn in (1, -1):
            q = Quaternion(axis, math.radians(3 * sgn))
            rotate_about('hand_r', q)
            z = net_min_z()
            rotate_about('hand_r', q.inverted())
            if best is None or z > best[0]:
                best = (z, q)
        if best[0] <= cur + 1e-3:
            break
        rotate_about('hand_r', best[1])


def settle_to_idle(t):
    """From T_STAND the whole pose converges exactly onto the idle base pose (no pop when the montage blends out)."""
    w = ease((t - T_STAND) / (DURATION - T_STAND))
    if w <= 0:
        return
    cur = {pb.name: pb.matrix_basis.copy() for pb in rig.pose.bones}
    for pb in rig.pose.bones:
        la, ra, sa = cur[pb.name].decompose()
        lb, rb, sb = base[pb.name].decompose()
        pb.matrix_basis = Matrix.LocRotScale(la.lerp(lb, w), ra.slerp(rb, w), sa.lerp(sb, w))
    upd()


# ---------------- bake ----------------
act = bpy.data.actions.new('Player_HitKnockdown')
act.use_fake_user = True
rig.animation_data.action = None
assign_action(rig, act)
report = []
for f in range(0, NFRAMES + 1):
    t = f / FPS
    pose_frame(t)
    settle_to_idle(t)
    for pb in rig.pose.bones:
        pb.keyframe_insert('location', frame=f, group=pb.name)
        pb.keyframe_insert('rotation_quaternion', frame=f, group=pb.name)
    mz = region_min_z(body_points())
    report.append((f, t, round(mz['body'] - FLOOR, 1), round(mz['l'] - FLOOR, 1), round(mz['r'] - FLOOR, 1),
                   round(net_min_z() - FLOOR, 1), round(head('pelvis').y - pelvis0.y, 1), round(head('pelvis').z - pelvis0.z, 1)))
for fc in getattr(act, 'fcurves', []):
    for k in fc.keyframe_points:
        k.interpolation = 'LINEAR'
scn.frame_start, scn.frame_end = 0, NFRAMES
scn.render.fps, scn.render.fps_base = FPS, 1.0
for r in report:
    print('FRAME f=%d t=%.2f bodyAboveFloor=%.1f footL=%.1f footR=%.1f netAboveFloor=%.1f pelvisBack=%.1f pelvisDZ=%.1f' % r)

# Last frame must be the idle pose (the montage blends back into the idle / locomotion state).
scn.frame_set(NFRAMES); upd()
end_pose = {pb.name: pb.matrix.copy() for pb in rig.pose.bones}
for pb in rig.pose.bones:
    pb.matrix_basis = base[pb.name]
upd()
err = max((end_pose[pb.name].translation - pb.matrix.translation).length for pb in rig.pose.bones)
print('END_POSE_MAX_BONE_OFFSET_CM', round(err, 2))
bpy.ops.wm.save_as_mainfile(filepath=OUT + 'Player_HitKnockdown.blend')

if DO_RENDER:
    import os
    os.makedirs(OUT + 'Preview', exist_ok=True)
    scn.frame_set(0)
    upd()
    floor_w = (A2W @ Vector((0, 0, FLOOR))).z
    cam = setup_preview(floor_w, res=(480, 420))
    views = {'side': ((5.2, .4, floor_w + 1.2), (0, .3, floor_w + .55)),
             'game': ((1.2, -4.2, floor_w + 2.4), (0, .2, floor_w + .6))}
    for name, (loc, tgt) in views.items():
        aim_camera(cam, loc, tgt)
        for f in [0, 3, 9, 13, 17, 19, 24, 30, 34, 39, 44, 48]:
            scn.frame_set(f)
            render(OUT + 'Preview/knock_%s_f%02d.png' % (name, f))

bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.ops.export_scene.fbx(filepath=OUT + 'Player_HitKnockdown.fbx', use_selection=True, object_types={'ARMATURE'},
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
                         bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0, use_armature_deform_only=False)
print('EXPORTED', OUT + 'Player_HitKnockdown.fbx')
