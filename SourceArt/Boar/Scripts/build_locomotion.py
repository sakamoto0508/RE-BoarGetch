"""移動系ループ。
A_Boar_Walk_Fixed : 既存AS_BoarWalkのボーン回転をそのまま使い、ルート（Hips）の縮尺0.92・前方31cmのズレだけIdle基準へ補正したもの（原本は変更しない）。
A_Boar_Trot       : BaseWalkSpeed 450cm/s で足滑り0の速歩（巡回・Normalの通常移動）。
A_Boar_Run        : 675cm/s（青ボア逃走 = 450×1.5）で足滑り0のギャロップ。頭はChargeより高く、尾を立てる。
Run: blender -b --python build_locomotion.py [-- --sheet]"""
import sys; sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/Boar/Scripts')
from boar_motion import *
S = lambda u, p=0.0: math.sin(2 * math.pi * (u - p))
C = lambda u, p=0.0: math.cos(2 * math.pi * (u - p))
SHEET = '--sheet' in sys.argv
out = {}

# ---------- Walk_Fixed ----------
reset()
b = Boar()
src = next(o for o in import_fbx(EXP + 'AS_BoarWalk.fbx') if o.type == 'ARMATURE')
sact = src.animation_data.action
f0, f1 = int(sact.frame_range[0]), int(sact.frame_range[1])
SW = src.scale.x  # 0.9235
r0 = b.root0.to_translation()
b.new_action('A_Boar_Walk_Fixed')
rep = Report('A_Boar_Walk_Fixed')
N = 30  # 原本は30fps・30フレーム(1.0秒)
scn = bpy.context.scene
for i in range(N + 1):
    scn.frame_set(f0 + (i % N))
    upd()
    basis = {pb.name: pb.matrix_basis.copy() for pb in src.pose.bones}
    loc = src.location.copy(); q = src.matrix_world.to_quaternion()
    b.rig.matrix_world = Matrix.Translation(Vector((loc.x + r0.x, loc.y + r0.y, loc.z / SW))) @ q.to_matrix().to_4x4()
    for pb in b.rig.pose.bones:
        pb.matrix_basis = basis[pb.name]
    upd()
    rep.add(i * 2, b, {l: True for l in LEGS}, {l: b.wtail(LEGS[l][3]).y for l in LEGS}, {l: False for l in LEGS})
# 地面合わせ：立脚中の蹄の最下点の中央値が0になるよう全体の高さを補正
lows = sorted(min(r['contact'][l] for l in LEGS) for r in rep.rows)
dz = -lows[len(lows) // 2] / 100.0
for i in range(N + 1):
    scn.frame_set(f0 + (i % N)); upd()
    basis = {pb.name: pb.matrix_basis.copy() for pb in src.pose.bones}
    loc = src.location.copy(); q = src.matrix_world.to_quaternion()
    b.rig.matrix_world = Matrix.Translation(Vector((loc.x + r0.x, loc.y + r0.y, loc.z / SW + dz))) @ q.to_matrix().to_4x4()
    for pb in b.rig.pose.bones:
        pb.matrix_basis = basis[pb.name]
    upd()
    b.key(i * 2)  # 60fpsへ（30fpsの各キーを2フレーム間隔で配置）
# 自然速度（立脚中の蹄先の移動）
ys = []
for i in range(N):
    scn.frame_set(f0 + i); upd()
    ys.append({l: (src.matrix_world @ src.pose.bones[LEGS[l][3]].tail) for l in LEGS})
vel = []
for l in LEGS:
    for a, c in zip(ys, ys[1:]):
        if a[l].z < 0.03 and c[l].z < 0.03:
            vel.append((c[l].y - a[l].y) / SW * 30)
out['walk_natural_speed_mps'] = sorted(vel)[len(vel) // 2]
out['walk_ground_dz_cm'] = dz * 100
bpy.data.objects.remove(src, do_unlink=True)
b.export('A_Boar_Walk_Fixed', N * 2)
if SHEET:
    setup_preview(b); contact_sheet(b, 'A_Boar_Walk_Fixed', [0, 10, 20, 30, 40, 50], views=('side',), cols=6)
bpy.ops.wm.save_as_mainfile(filepath=ROOT + 'Blend/A_Boar_Walk_Fixed.blend')

# ---------- Trot ----------
reset()
b = Boar()
P = dict(frames=16, speed=4.5, duty=0.38,
         phase={'FL': 0.0, 'HR': 0.03, 'FR': 0.5, 'HL': 0.53},
         reach_f=-0.07, reach_h=-0.06, lift_f=0.075, lift_h=0.06, narrow=0.01,
         hoof_curl=70, pastern_curl=35, toe_push=15, height=-0.008,
         bob=lambda u: 0.009 * C(2 * u, 0.2),
         pitch=lambda u: 0.8 * S(2 * u, 0.1), roll=lambda u: 1.2 * S(u, 0.0),
         chest=lambda u: 0.8 * S(2 * u, 0.3),
         head=lambda u: 4.0 - 1.5 * S(2 * u, 0.15), head_yaw=lambda u: 1.5 * S(u, 0.1),
         tail=lambda u, i: (6 if i == 0 else 2) + 4 * S(u, i * 0.15))
rep, st = build_loop(b, 'A_Boar_Trot', P)
out['trot'] = rep.save({'speed': P['speed']})
b.export('A_Boar_Trot', P['frames'])
if SHEET:
    setup_preview(b); contact_sheet(b, 'A_Boar_Trot', [0, 2, 4, 6, 8, 10, 12, 14], views=('side',), cols=4)
bpy.ops.wm.save_as_mainfile(filepath=ROOT + 'Blend/A_Boar_Trot.blend')

# ---------- Run ----------
reset()
b = Boar()
P = dict(frames=16, speed=6.75, duty=0.24,
         phase={'HL': 0.0, 'HR': 0.1, 'FL': 0.42, 'FR': 0.52},
         reach_f=-0.08, reach_h=-0.05, lift_f=0.10, lift_h=0.085, narrow=0.012,
         hoof_curl=80, pastern_curl=38, toe_push=20, height=-0.006,
         bob=lambda u: 0.026 * C(u, 0.86) - 0.004 * C(2 * u, 0.2),
         pitch=lambda u: 0.5 + 2.5 * S(u, 0.12), roll=lambda u: 1.4 * S(u, 0.25),
         chest=lambda u: 2.0 * S(u, 0.35),
         head=lambda u: 6.0 - 0.6 * (2.5 * S(u, 0.12) + 2.0 * S(u, 0.35)),
         tail=lambda u, i: (24 if i == 0 else 10) + 6 * S(u, 0.1 + i * 0.12))
rep, st = build_loop(b, 'A_Boar_Run', P)
out['run'] = rep.save({'speed': P['speed']})
b.export('A_Boar_Run', P['frames'])
if SHEET:
    setup_preview(b); contact_sheet(b, 'A_Boar_Run', [0, 2, 4, 6, 8, 10, 12, 14], views=('side',), cols=4)
bpy.ops.wm.save_as_mainfile(filepath=ROOT + 'Blend/A_Boar_Run.blend')
print('LOCO', json.dumps(out))
