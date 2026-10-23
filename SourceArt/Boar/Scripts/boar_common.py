"""ボアのアニメーション制作用 共通ヘルパー（Blender 5.x, `blender -b --python <build>.py`）。

マスターリグ = UEから書き出した A_Boar_Idle.fbx のアーマチュア（オブジェクト名 'Hips' = UE Skeletonのルートボーン）。
ルートボーンの位置・回転はオブジェクトTransformとして保持し、FBX書き出し時に 'Hips' ボーンとして戻る。
Mesh（Boar_Mesh.fbx）はマスターリグへ再バインドして、接地・貫通の検証とプレビュー描画にだけ使う。

座標（Blender World, m）: ボアは -Y を向き、+Z が上、+X がボアの左側。
UE上ではルートモーションを使わない（インプレース）。移動はCharacterMovementが担当する。
"""
import bpy, math, json
from mathutils import Matrix, Quaternion, Vector, Euler

ROOT = 'C:/UnrealGames/REBoarGetch/SourceArt/Boar/'
EXP = ROOT + 'UEExport/'
FPS = 60

LEGS = {
    'FL': ('frontleg', 'frontleg0', 'frontleg1', 'frontleg2'),
    'FR': ('R_frontleg', 'R_frontleg0', 'R_frontleg1', 'R_frontleg2'),
    'HL': ('backleg', 'backleg0', 'backleg1', 'backleg2'),
    'HR': ('R_backleg', 'R_backleg0', 'R_backleg1', 'R_backleg2'),
}
TAIL = ('tail', 'tailstart', 'tail1', 'tail2', 'tail3')
X = Vector((1, 0, 0))
Y = Vector((0, 1, 0))
Z = Vector((0, 0, 1))


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scn = bpy.context.scene
    scn.render.fps = FPS
    scn.render.fps_base = 1.0


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before]


def upd():
    bpy.context.view_layer.update()


def smooth(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def smoother(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * x * (x * (x * 6 - 15) + 10)


def lerp(a, b, t):
    return a + (b - a) * t


def ramp(t, t0, t1):
    """t0→t1 を0→1へ（smoothstep）。"""
    if t1 <= t0:
        return 1.0 if t >= t1 else 0.0
    return smooth((t - t0) / (t1 - t0))


def bump(t, t0, t1, t2):
    """t0で0→t1で1→t2で0 の滑らかな山。"""
    if t <= t0 or t >= t2:
        return 0.0
    if t < t1:
        return smooth((t - t0) / (t1 - t0))
    return 1.0 - smooth((t - t1) / (t2 - t1))


def rot(axis, deg):
    return Quaternion(axis, math.radians(deg))


class Boar:
    """マスターリグ・Idle基準姿勢・Meshを保持し、1フレーム分の姿勢を組み立てる。"""

    def __init__(self, with_mesh=True):
        objs = import_fbx(EXP + 'A_Boar_Idle.fbx')
        self.rig = next(o for o in objs if o.type == 'ARMATURE')
        self.rig.name = 'Hips'
        self.rig.data.name = 'Hips'
        act = self.rig.animation_data.action
        act.name = 'A_Boar_Idle_Src'
        bpy.context.scene.frame_set(1)
        upd()
        # Idle姿勢（ローカル）を基準として保存する。
        self.idle_basis = {pb.name: pb.matrix_basis.copy() for pb in self.rig.pose.bones}
        self.root0 = self.rig.matrix_world.copy()
        self.rig.animation_data.action = None
        self.rig.rotation_mode = 'QUATERNION'
        for pb in self.rig.pose.bones:
            pb.rotation_mode = 'QUATERNION'
        self.apply_idle()
        # Idle姿勢での各骨のWorld情報（脚IKの基準）。
        self.idle_world = {pb.name: (self.rig.matrix_world @ pb.matrix).copy() for pb in self.rig.pose.bones}
        self.body = None
        if with_mesh:
            self._load_mesh()
        self.ground = self.measure_ground()

    # ---------------- Mesh ----------------
    def _load_mesh(self):
        mobjs = import_fbx(EXP + 'Boar_Mesh.fbx')
        body = next(o for o in mobjs if o.type == 'MESH')
        upd()
        mw = body.matrix_world.copy()
        body.parent = None
        body.matrix_world = mw
        for mod in body.modifiers:
            if mod.type == 'ARMATURE':
                mod.object = self.rig
        body.parent = self.rig
        body.matrix_parent_inverse = self.rig.matrix_world.inverted()
        for o in mobjs:
            if o is not body:
                bpy.data.objects.remove(o, do_unlink=True)
        body.name = 'BoarBody'
        self.body = body
        # 蹄（脚の末端2骨）に属する頂点。接地測定に使う。
        self.hoof_verts = {}
        for leg, chain in LEGS.items():
            gi = {body.vertex_groups[n].index for n in chain[2:] if n in body.vertex_groups}
            self.hoof_verts[leg] = [v.index for v in body.data.vertices
                                    if any(g.group in gi and g.weight > 0.5 for g in v.groups)]
        hg = {body.vertex_groups[n].index for n in ('head', 'headend') if n in body.vertex_groups}
        self.head_verts = [v.index for v in body.data.vertices if any(g.group in hg and g.weight > 0.5 for g in v.groups)]
        upd()

    def mesh_world_verts(self):
        dg = bpy.context.evaluated_depsgraph_get()
        e = self.body.evaluated_get(dg)
        mw = self.body.matrix_world
        return [mw @ v.co for v in e.data.vertices]

    def measure_ground(self):
        """Idle姿勢での最下点を地面（ユーザー確認済みの既存Idleと同じ接地関係）とする。"""
        if self.body is None:
            return 0.0
        self.apply_idle()
        vs = self.mesh_world_verts()
        return min(v.z for v in vs)

    def contact_report(self):
        """各蹄の最下点高さ（地面基準, cm）と、全頂点の最下点（貫通チェック）を返す。"""
        vs = self.mesh_world_verts()
        rep = {leg: (min(vs[i].z for i in idx) - self.ground) * 100 for leg, idx in self.hoof_verts.items()}
        rep['min'] = (min(v.z for v in vs) - self.ground) * 100
        rep['head'] = (min(vs[i].z for i in self.head_verts) - self.ground) * 100
        lo = min(range(len(vs)), key=lambda i: vs[i].z)
        g = max(self.body.data.vertices[lo].groups, key=lambda g: g.weight, default=None)
        rep['lowest_part'] = self.body.vertex_groups[g.group].name if g else '?'
        return rep

    # ---------------- 姿勢の組み立て ----------------
    def apply_idle(self):
        self.rig.matrix_world = self.root0.copy()
        for pb in self.rig.pose.bones:
            pb.matrix_basis = self.idle_basis[pb.name].copy()
        upd()

    def pb(self, n):
        return self.rig.pose.bones[n]

    def wmat(self, n):
        return self.rig.matrix_world @ self.pb(n).matrix

    def whead(self, n):
        return self.rig.matrix_world @ self.pb(n).head

    def wtail(self, n):
        return self.rig.matrix_world @ self.pb(n).tail

    def set_wmat(self, n, m):
        self.pb(n).matrix = self.rig.matrix_world.inverted() @ m
        upd()

    def set_root(self, offset=Vector(), pitch=0.0, roll=0.0, yaw=0.0):
        """ルート（Hips）を基準位置から動かす。pitch+ = 頭が下がる（+X軸回り）。回転中心は腰。"""
        q = rot(Z, yaw) @ rot(X, pitch) @ rot(Y, roll)
        r0 = self.root0
        piv = r0.to_translation()
        m = Matrix.Translation(piv + offset) @ q.to_matrix().to_4x4() @ Matrix.Translation(-piv) @ r0
        self.rig.matrix_world = m
        upd()

    def root_q(self):
        return (self.rig.matrix_world.to_quaternion() @ self.root0.to_quaternion().inverted())

    def rotate_world(self, n, q, pivot=None):
        """骨nを、World軸の回転qで自身の付け根（またはpivot）回りに回す。子骨は追従する。"""
        m = self.wmat(n)
        p = m.to_translation() if pivot is None else pivot
        self.set_wmat(n, Matrix.Translation(p) @ q.to_matrix().to_4x4() @ Matrix.Translation(-p) @ m)

    def rotate_local(self, n, pitch=0.0, yaw=0.0, roll=0.0):
        """ボアの体軸基準（ルートの向き）で回す。pitch+ = 先端が下がる。"""
        rq = self.root_q()
        q = rq @ rot(Z, yaw) @ rot(X, pitch) @ rot(Y, roll) @ rq.inverted()
        self.rotate_world(n, q)

    def aim_bone(self, n, direction):
        m = self.wmat(n)
        yv = (m.to_3x3() @ Vector((0, 1, 0))).normalized()
        self.rotate_world(n, yv.rotation_difference(direction.normalized()))

    # ---------------- 脚IK ----------------
    def idle_foot(self, leg):
        """Idleでの蹄先端（World）。"""
        return self.idle_world[LEGS[leg][3]] @ Vector((0, self.pb(LEGS[leg][3]).bone.length, 0))

    def leg_ik(self, leg, tip, hoof_pitch=0.0, pastern_pitch=0.0, splay=0.0):
        """蹄の先端をtip（World）へ置く。
        hoof_pitch / pastern_pitch: 蹄・繋ぎをIdle時の向きから体の横軸回りに曲げる角度（+で蹄先が下・後ろへ巻く）。
        上2骨は2ボーンIKで解き、膝・肘の向きはIdle姿勢の曲がり方向を保つ。"""
        a, b, c, d = LEGS[leg]
        rq = self.root_q()
        yaw_only = rot(Z, math.degrees(rq.to_euler('ZXY').z))
        lat = rq @ X
        # 蹄・繋ぎの目標方向（Idleの向きを、ヨーだけ追従させて地面に対して保つ）
        dD = yaw_only @ (self.idle_world[d].to_3x3() @ Y).normalized()
        dC = yaw_only @ (self.idle_world[c].to_3x3() @ Y).normalized()
        dD = rot(lat, hoof_pitch + pastern_pitch) @ dD
        dC = rot(lat, pastern_pitch) @ dC
        lD = self.pb(d).bone.length
        lC = self.pb(c).bone.length
        d_head = tip - dD * lD
        c_head = d_head - dC * lC
        # 2ボーンIK（a, b）
        J = self.whead(a)
        l1 = self.pb(a).bone.length
        l2 = self.pb(b).bone.length
        v = c_head - J
        dist = v.length
        dist_c = min(max(dist, abs(l1 - l2) + 1e-4), (l1 + l2) * 0.9995)
        u = v.normalized()
        # 曲げ方向：Idleでの膝位置を現在のルート姿勢へ回したもの
        idle_knee = self.idle_world[a] @ Vector((0, l1, 0))
        idle_J = self.idle_world[a].to_translation()
        pole = rq @ (idle_knee - idle_J)
        pole = rot(Z, 0) @ pole
        w = (pole - u * pole.dot(u))
        if w.length < 1e-6:
            w = rq @ Y
        w.normalize()
        ca = (l1 * l1 + dist_c * dist_c - l2 * l2) / (2 * l1 * dist_c)
        ca = min(max(ca, -1.0), 1.0)
        sa = math.sqrt(max(0.0, 1 - ca * ca))
        K = J + (u * ca + w * sa) * l1
        self.aim_bone(a, K - J)
        self.aim_bone(b, c_head - self.whead(b))
        self.aim_bone(c, dC)
        self.aim_bone(d, dD)
        return dist > (l1 + l2) * 0.9995  # 届かない場合True

    # ---------------- キー・書き出し ----------------
    def key(self, frame):
        r = self.rig
        r.keyframe_insert('location', frame=frame)
        r.keyframe_insert('rotation_quaternion', frame=frame)
        r.keyframe_insert('scale', frame=frame)
        for pb in r.pose.bones:
            pb.keyframe_insert('location', frame=frame)
            pb.keyframe_insert('rotation_quaternion', frame=frame)
            pb.keyframe_insert('scale', frame=frame)

    def new_action(self, name):
        r = self.rig
        r.animation_data_create()
        act = bpy.data.actions.new(name)
        act.use_fake_user = True
        r.animation_data.action = act
        return act

    def export(self, name, nframes):
        scn = bpy.context.scene
        scn.render.fps = FPS
        scn.render.fps_base = 1.0
        scn.frame_start = 0
        scn.frame_end = nframes
        bpy.ops.object.select_all(action='DESELECT')
        self.rig.select_set(True)
        bpy.context.view_layer.objects.active = self.rig
        path = ROOT + 'FBX/' + name + '.fbx'
        bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'ARMATURE'},
                                 add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=False,
                                 bake_anim_use_nla_strips=False, bake_anim_force_startend_keying=True,
                                 bake_anim_simplify_factor=0.0, use_armature_deform_only=False,
                                 apply_unit_scale=True, global_scale=1.0)
        print('EXPORTED', path)
        return path


# ---------------- 歩容 ----------------
def gait_foot(u, duty, stride, lift, reach_bias=0.0):
    """1脚の足先オフセット（体基準, m）。u: 0-1の位相。立脚中は+Y（後方）へ等速、遊脚で前へ戻る。
    戻り値 (dy, dz, swing_s)  swing_s は遊脚進行度（立脚中は-1）。"""
    u = u % 1.0
    if u < duty:
        s = u / duty
        return (-stride / 2 + stride * s + reach_bias, 0.0, -1.0)
    s = (u - duty) / (1 - duty)
    # 前半で素早く持ち上げ、後半で前へ伸ばして着地する
    y = stride / 2 - stride * smoother(s) + reach_bias
    z = lift * math.sin(math.pi * min(1.0, s * 1.08)) ** 0.8 if s < 0.93 else lift * max(0.0, (1 - s) / 0.07) * 0.2
    return (y, max(z, 0.0), s)


# ---------------- プレビュー ----------------
def setup_preview(boar):
    scn = bpy.context.scene
    scn.render.engine = 'BLENDER_WORKBENCH'
    scn.display.shading.light = 'STUDIO'
    scn.display.shading.color_type = 'OBJECT'
    if boar.body:
        boar.body.color = (0.62, 0.45, 0.34, 1)
    scn.display.shading.show_shadows = True
    scn.display.shading.show_cavity = True
    scn.render.resolution_x = 480
    scn.render.resolution_y = 360
    scn.render.film_transparent = False
    bpy.ops.mesh.primitive_plane_add(size=12, location=(0, 0, boar.ground))
    plane = bpy.context.active_object
    mat = bpy.data.materials.new('Ground')
    plane.data.materials.append(mat)
    plane.color = (0.78, 0.84, 0.74, 1)
    # 足滑り確認用の目盛り（20cm間隔の線）
    for i in range(-15, 16):
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0, i * 0.2, boar.ground + 0.0005))
        c = bpy.context.active_object
        c.scale = (1.6, 0.006, 0.001)
        c.color = (0.35, 0.4, 0.35, 1)
    cam_data = bpy.data.cameras.new('Cam')
    cam = bpy.data.objects.new('Cam', cam_data)
    scn.collection.objects.link(cam)
    scn.camera = cam
    cam_data.lens = 45
    return cam


def aim_camera(cam, loc, target):
    cam.location = loc
    d = Vector(target) - Vector(loc)
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


def render(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
