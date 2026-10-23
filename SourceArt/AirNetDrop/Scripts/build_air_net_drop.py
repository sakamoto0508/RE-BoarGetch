"""Builds the Air Net Drop clip set on the existing player skeleton (rig exported from UE, see SourceArt/StageClear/UEExport)
with the real net attached to hand_r through the in-game socket (hand_r_net_socket).

Clips (30 fps, in place: no root motion, the dive itself is CharacterMovement in C++):
  A_Player_AirNetDrop_Anticipation  short gather: knees tuck, back rounds, both hands pull the net back over the right shoulder
  A_Player_AirNetDrop_Swing         two-handed chop: net arcs over the top to forward-down, torso pitches forward, legs tuck
  A_Player_AirNetDrop_DropLoop      loopable dive hold: weight on the net pointing forward-down, legs trail back
  A_Player_AirNetDrop_Recovery      net pulled back to the carry pose, returns to the FallLoop pose
  A_Player_AirNetDrop_Land          heavy two-foot landing with the net slapped forward on the ground, back to Idle

Every frame is checked against the deformed body (net vertices inside the body, minimum clearance) and the left hand distance
to the shaft is reported.  Run: blender -b --python build_air_net_drop.py [-- --render]"""
import sys, math, json
sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/StageClear/Scripts')
import bpy
from mathutils import Matrix, Quaternion, Vector
from mathutils.bvhtree import BVHTree
from victory_common import reset, load_rig, attach_net, assign_action, load_action, net_points_world, setup_preview, aim_camera, render

OUT = 'C:/UnrealGames/REBoarGetch/SourceArt/AirNetDrop/'
FPS = 30
DO_RENDER = '--render' in sys.argv

reset()
rig, body, idle_act = load_rig()
net = attach_net(rig)
fall_act = load_action(OUT + 'UEExport/A_Player_FallLoop.fbx', 'A_Player_FallLoop')
scn = bpy.context.scene
A2W = rig.matrix_world
W2A = A2W.inverted()
FWD = Vector((0, -1, 0))   # character forward in rig space (cm), +X = character left, +Z = up


def upd():
    bpy.context.view_layer.update()


def capture_base(act):
    assign_action(rig, act)
    scn.frame_set(1)
    upd()
    b = {pb.name: pb.matrix_basis.copy() for pb in rig.pose.bones}
    rig.animation_data.action = None
    return b


BASE = {'fall': capture_base(fall_act), 'idle': capture_base(idle_act)}
for pb in rig.pose.bones:
    pb.rotation_mode = 'QUATERNION'


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


def set_base(name):
    for pb in rig.pose.bones:
        pb.matrix_basis = BASE[name][pb.name]
    upd()


def rx(deg):
    return Matrix.Rotation(math.radians(deg), 3, 'X')


def net_axis():
    g, h = [W2A @ p for p in net_points_world(net)]
    return g, (h - g).normalized()


def net_dir(theta, nx):
    """Sagittal angle theta (0 = up, 90 = forward, 135 = forward-down) plus a sideways component nx (+ = character left)."""
    t = math.radians(theta)
    return Vector((nx, -math.sin(t), math.cos(t))).normalized()


def smooth(a, b, t):
    t = min(1.0, max(0.0, (t - a) / (b - a))) if b > a else 1.0
    return t * t * (3 - 2 * t)


# ---------------- reference values of the two base poses ----------------
REF = {}
for name in ('fall', 'idle'):
    set_base(name)
    g, d = net_axis()
    hr = head('hand_r')
    # point of the shaft closest to the right wrist: the palm offset is kept when the left hand grabs the shaft
    s_r = (hr - g).dot(d)
    sh = (head('upperarm_l') + head('upperarm_r')) * .5
    REF[name] = {
        'theta': math.degrees(math.atan2(-d.y, d.z)), 'nx': d.x / max(1e-4, Vector((0, d.y, d.z)).length),
        'hand_r_rel': hr - sh, 'hand_l': head('hand_l'), 's_r': s_r, 'palm': hr - (g + d * s_r),
        'feet': {s: head('foot_' + s) for s in 'lr'},
        'feet_pelvis': {s: rig.pose.bones['pelvis'].matrix.inverted() @ head('foot_' + s) for s in 'lr'},
        'footrot': {s: rig.pose.bones['foot_' + s].matrix.to_3x3().copy() for s in 'lr'},
        'pelvis': head('pelvis'),
    }
    print('REF', name, 'theta %.1f nx %.2f s_r %.1f palm %.1f' % (REF[name]['theta'], REF[name]['nx'], s_r, REF[name]['palm'].length),
          'hand_r_rel', tuple(round(x, 1) for x in REF[name]['hand_r_rel']))
PALM = REF['fall']['palm'].length
GRIP_GAP = 15.0   # cm along the shaft from the right hand to the left hand (toward the net head)


# ---------------- pose description ----------------
# A key is a dict of channels; missing channels fall back to the base pose values.
#   twist: chest yaw toward the net side (deg, - = right)
#   pitch: whole body forward pitch at the pelvis (deg)    curl: spine forward bend (deg)   headc: head counter pitch (deg)
#   pz: pelvis translation z (cm)                          theta/nx: net direction           roll: hoop roll bias (deg)
#   hp/hr/hs/hz: right hand polar position around the shoulder centre in the chest frame (phi deg, reach cm, side cm, extra z)
#   wl: left hand on the shaft weight                       wh: hand IK weight (0 = base arm pose)
#   tuck: (dy, dz) foot offset in the pelvis frame (cm)     toe: foot point (deg)   lf: left/right foot asymmetry (cm)
def lerp(a, b, t):
    return a + (b - a) * t


def blend_keys(k0, k1, t):
    out = {}
    for k in k0:
        a, b = k0[k], k1[k]
        out[k] = tuple(lerp(x, y, t) for x, y in zip(a, b)) if isinstance(a, tuple) else lerp(a, b, t)
    return out


def hand_polar_from_base(name):
    """Express the base right wrist as (phi, reach, side, z) around the shoulder centre (chest frame = unpitched)."""
    r = REF[name]['hand_r_rel']
    reach = math.hypot(r.y, r.z)
    return math.degrees(math.atan2(-r.y, r.z)), reach, r.x, 0.0


def key(**kw):
    k = dict(pitch=0.0, curl=0.0, twist=0.0, headc=0.0, pz=0.0, theta=0.0, nx=0.0, roll=0.0, hp=0.0, hr=30.0, hs=-10.0, hz=0.0,
             wl=0.0, gl=15.0, wh=1.0, tuck=(0.0, 0.0), toe=0.0, lf=0.0, ground=0.0)
    k.update(kw)
    return k


fp = hand_polar_from_base('fall')
ip = hand_polar_from_base('idle')
K_FALL = key(theta=REF['fall']['theta'], nx=REF['fall']['nx'], hp=fp[0], hr=fp[1], hs=fp[2], wh=0.0)
K_IDLE = key(theta=REF['idle']['theta'], nx=REF['idle']['nx'], hp=ip[0], hr=ip[1], hs=ip[2], wh=0.0, ground=1.0)
print('FALL hand polar', fp, 'IDLE hand polar', ip)

P = json.load(open(OUT + 'Scripts/air_net_drop_keys.json'))


def K(name, **over):
    d = dict(P[name])
    d.update(over)
    for k, v in list(d.items()):
        if isinstance(v, list):
            d[k] = tuple(v)
    return key(**d)


K_WIND = K('wind')
K_MID = K('swing_mid')
K_OVER = K('swing_over')
K_DROP = K('drop')
K_LAND = K('land_impact')
K_LAND2 = K('land_absorb')
K_RISE = K('land_rise')


def ease_out(t):
    return 1 - (1 - t) ** 3


def ease_in(t):
    return t ** 2.2


def sample(clip, t):
    """Pose channels for a clip at normalized time t (0..1)."""
    if clip == 'Anticipation':
        return blend_keys(K_FALL, K_WIND, ease_out(t))
    if clip == 'Swing':
        # quick heavy chop: slow start from the wind-up, fast through the top, overshoot, settle into the dive hold
        if t < .45:
            return blend_keys(K_WIND, K_MID, ease_in(t / .45))
        if t < .75:
            return blend_keys(K_MID, K_OVER, smooth(0, 1, (t - .45) / .30) * .5 + (t - .45) / .30 * .5)
        return blend_keys(K_OVER, K_DROP, smooth(.75, 1.0, t))
    if clip == 'DropLoop':
        k = dict(K_DROP)
        s = math.sin(t * 2 * math.pi)
        c = math.cos(t * 2 * math.pi)
        k['theta'] += 2.5 * s
        k['pitch'] += 1.5 * c
        k['tuck'] = (k['tuck'][0] + 2.5 * s, k['tuck'][1] + 1.5 * c)
        k['lf'] += 3.0 * s
        return k
    if clip == 'Recovery':
        return blend_keys(K_DROP, K_FALL, smooth(0, 1, t))
    if clip == 'Land':
        if t < .18:
            return blend_keys(K_LAND, K_LAND2, ease_out(t / .18))
        if t < .55:
            return blend_keys(K_LAND2, K_RISE, smooth(.18, .55, t))
        return blend_keys(K_RISE, K_IDLE, smooth(.55, 1.0, t))
    raise ValueError(clip)


CLIPS = {  # name: (frames incl. last, base pose)
    'Anticipation': (6, 'fall'),
    'Swing': (9, 'fall'),
    'DropLoop': (12, 'fall'),
    'Recovery': (12, 'fall'),
    'Land': (15, 'idle'),
}


def apply(k, base):
    set_base(base)
    ref = REF[base]
    # pelvis height and whole-body pitch
    pb = rig.pose.bones['pelvis']
    pb.matrix = Matrix.Translation((0, 0, k['pz'])) @ pb.matrix
    upd()
    rotate_about('pelvis', rx(k['pitch']).to_quaternion())
    # legs
    pel = rig.pose.bones['pelvis'].matrix.copy()
    for s, side in (('l', 1), ('r', -1)):
        asym = k['lf'] * (1 if s == 'l' else -1)
        if k['ground'] >= .5:
            target = ref['feet'][s].copy()
            hint = (FWD + Vector((side * .25, 0, 0))).normalized()
        else:
            local = ref['feet_pelvis'][s]
            target = pel @ local
            # tuck / trail is applied in the pitched body frame so it follows the dive
            target = target + rx(k['pitch']) @ Vector((0, k['tuck'][0] + asym * .5, k['tuck'][1] + asym))
            hint = rx(k['pitch']) @ (FWD + Vector((side * .25, 0, .2))).normalized()
        two_bone('thigh_' + s, 'calf_' + s, target, hint)
        rot = ref['footrot'][s] if k['ground'] >= .5 else (rx(k['pitch']) @ ref['footrot'][s])
        pbf = rig.pose.bones['foot_' + s]
        pbf.matrix = Matrix.Translation(pbf.head) @ (Matrix.Rotation(-math.radians(k['toe']), 3, 'X') @ rot).to_4x4()
        upd()
    # spine curl (forward), head counter rotation keeps the eyes on the landing zone
    spine_up = rx(k['pitch']) @ Vector((0, 0, 1))
    for n, w in (('spine_01', .25), ('spine_02', .35), ('spine_03', .40)):
        rotate_about(n, Quaternion(spine_up, math.radians(k['twist'] * w)) @ rx(k['curl'] * w).to_quaternion())
    rotate_about('head', rx(k['headc']).to_quaternion())
    chest = Matrix.Rotation(math.radians(k['twist']), 3, spine_up) @ rx(k['pitch'] + k['curl'])
    # right hand: polar position around the shoulder centre in the chest frame, then the wrist turns the net onto theta
    if k['wh'] > 1e-3:
        sh = (head('upperarm_l') + head('upperarm_r')) * .5
        ph = math.radians(k['hp'])
        p_r = sh + chest @ Vector((k['hs'], -math.sin(ph) * k['hr'], math.cos(ph) * k['hr'] + k['hz']))
        base_r = head('hand_r')
        p_r = base_r.lerp(p_r, k['wh'])
        two_bone('upperarm_r', 'lowerarm_r', p_r, chest @ Vector((-.75, .35, -.55)))
    g, d = net_axis()
    want = net_dir(k['theta'], k['nx'])
    if k['wh'] > 1e-3:
        want = d.slerp(want, k['wh']) if hasattr(d, 'slerp') else want
    rotate_about('hand_r', d.rotation_difference(want))
    # hoop roll: rotate about the shaft so the bag trails upward (opening faces the travel / landing direction)
    g, d = net_axis()
    if k['wh'] > 1e-3:
        bag = (net.matrix_world.to_3x3() @ Vector((0, -1, 0)))
        bag = (W2A.to_3x3() @ bag)
        bag = (bag - d * bag.dot(d)).normalized()
        up = Vector((0, 0, 1))
        trail = (up - d * up.dot(d))
        if trail.length > 1e-3:
            trail = trail.normalized()
            ang = math.atan2(d.dot(bag.cross(trail)), bag.dot(trail)) * k['wh']
            ang += math.radians(k['roll'])
            rotate_about('hand_r', Quaternion(d, ang), pivot=g + d * (head('hand_r') - g).dot(d))
    # left hand on the shaft (toward the net head) keeping the same palm offset as the right hand
    g, d = net_axis()
    hr_ = head('hand_r')
    s_r = (hr_ - g).dot(d)
    palm = hr_ - (g + d * s_r)
    grip_l = g + d * (s_r + k['gl']) + palm
    t_l = ref['hand_l'].lerp(grip_l, k['wl']) if k['wh'] > 1e-3 else head('hand_l')
    if k['wl'] > 1e-3:
        two_bone('upperarm_l', 'lowerarm_l', t_l, chest @ Vector((.75, .35, -.55)))
        # palm along the shaft like the right hand
        aim('hand_l', (rig.pose.bones['hand_r'].matrix.to_3x3() @ Vector((0, 1, 0))).lerp(
            rig.pose.bones['hand_l'].matrix.to_3x3() @ Vector((0, 1, 0)), 1 - k['wl']))
    return (head('hand_l') - grip_l).length if k['wl'] > .5 else 0.0


# ---------------- bake + checks ----------------
report = {}
actions = {}
deps = None
for clip, (nf, base) in CLIPS.items():
    act = bpy.data.actions.new('A_Player_AirNetDrop_' + clip)
    act.use_fake_user = True
    rig.animation_data.action = None
    assign_action(rig, act)
    rows = []
    for f in range(0, nf + 1):
        t = f / nf
        k = sample(clip, t if clip != 'DropLoop' else (f % nf) / nf)
        lgap = apply(k, base)
        for pb in rig.pose.bones:
            pb.keyframe_insert('location', frame=f, group=pb.name)
            pb.keyframe_insert('rotation_quaternion', frame=f, group=pb.name)
        dg = bpy.context.evaluated_depsgraph_get()
        bvh = BVHTree.FromObject(body, dg)
        inv = body.matrix_world.inverted()
        inside, clearance = 0, 1e9
        for v in net.data.vertices:
            lv = inv @ (net.matrix_world @ v.co)
            loc, nrm, idx, dist = bvh.find_nearest(lv)
            if loc is None:
                continue
            # body-local units are cm: a net vertex behind the skin surface within 12 cm is inside the body
            if (lv - loc).dot(nrm) < 0 and dist < 12.0:
                inside += 1
            clearance = min(clearance, dist)
        ev = body.evaluated_get(dg)
        me = ev.to_mesh()
        zs = [(body.matrix_world @ v.co).z * 100 for v in me.vertices]
        ev.to_mesh_clear()
        g, h = [W2A @ p for p in net_points_world(net)]
        rows.append({'f': f, 'inside': inside, 'clear_cm': round(clearance, 1), 'lhand_gap_cm': round(lgap, 1),
                     'body_min_z': round(min(zs), 1), 'net_head': [round(x, 1) for x in h], 'net_butt': [round(x, 1) for x in g]})
    for fc in (act.fcurves if hasattr(act, 'fcurves') else []):
        for kp in fc.keyframe_points:
            kp.interpolation = 'LINEAR'
    actions[clip] = (act, nf)
    report[clip] = rows
    for r in rows:
        print('CHK %-12s f%02d inside=%3d clear=%5.1f lgap=%4.1f bodyMinZ=%6.1f head=%s butt=%s' % (
            clip, r['f'], r['inside'], r['clear_cm'], r['lhand_gap_cm'], r['body_min_z'], r['net_head'], r['net_butt']))
json.dump(report, open(OUT + 'Preview/check_report.json', 'w'), indent=1)
scn.render.fps, scn.render.fps_base = FPS, 1.0
bpy.ops.wm.save_as_mainfile(filepath=OUT + 'Player_AirNetDrop.blend')

if DO_RENDER:
    import os
    os.makedirs(OUT + 'Preview/frames', exist_ok=True)
    assign_action(rig, actions['Land'][0]); scn.frame_set(15); upd()
    floor = min((A2W @ head('ball_l')).z, (A2W @ head('ball_r')).z) - .07
    cam = setup_preview(floor, res=(420, 420))
    plane = next(o for o in scn.objects if o.type == 'MESH' and o.name.startswith('Plane'))
    # camera offsets (m) from a fixed point in front of the pelvis: side, front 3/4, TPS back 3/4
    views = {'side': Vector((3.3, -.6, .25)), 'front': Vector((1.6, -3.2, .5)), 'back': Vector((-1.3, 2.9, 1.0))}
    for clip, (act, nf) in actions.items():
        rig.animation_data.action = None
        assign_action(rig, act)
        plane.hide_render = clip != 'Land'
        scn.frame_set(0); upd()
        tgt = A2W @ (head('pelvis') + Vector((0, -45, -5)))
        for f in range(0, nf + 1, 3 if nf > 9 else 2):
            scn.frame_set(f)
            for vn, off in views.items():
                aim_camera(cam, tgt + off, tgt)
                render(OUT + 'Preview/frames/%s_%s_f%02d.png' % (clip, vn, f))

# ---------------- export one FBX per clip (armature + animation only) ----------------
for clip, (act, nf) in actions.items():
    rig.animation_data.action = None
    assign_action(rig, act)
    scn.frame_start, scn.frame_end = 0, nf
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    path = OUT + 'FBX/A_Player_AirNetDrop_%s.fbx' % clip
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'ARMATURE'},
                             add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
                             bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0, use_armature_deform_only=False)
    print('EXPORTED', path)
