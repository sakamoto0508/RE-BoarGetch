"""A_Boar_ChargeRecovery：突進後の停止・後退・復帰（1.0秒）。
0.00-0.08 着弾：前脚を突っ張って滑り、体が前へつんのめる → 0.08-0.20 反動で頭が跳ね上がる →
0.15-0.75 後退（AttackRetreatSpeed 300cm/s相当の後ずさり。立脚の足は体に対して前へ流れる）＋目を回したように頭を振る →
0.75-1.00 脚を揃えて通常姿勢へ戻る。後退距離・速度はゲーム側（StateTree / DataAsset）のまま。
Run: blender -b --python build_recovery.py [-- --sheet]"""
import sys; sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/Boar/Scripts')
from boar_motion import *
N = 60
RETREAT = 3.0  # m/s（DataAsset AttackRetreatSpeed 300cm/s）


def vback(t):
    return RETREAT * ramp(t, 0.02, 0.15) * (1 - ramp(t, 0.70, 0.80))


drift = lambda t: -vback(t)  # 後退中、立脚の足は体に対して前(-Y)へ流れる
K = [0.0, 0.06, 0.14, 0.30, 0.50, 0.70, 0.85, 1.0]


def body(t):
    shake = math.sin(6.283 * 3.2 * (t - 0.28)) * ramp(t, 0.26, 0.34) * (1 - ramp(t, 0.6, 0.86))
    return dict(
        dy=keys(t, list(zip(K, [-0.03, -0.055, 0.02, 0.03, 0.03, 0.02, 0.0, 0.0]))),
        dz=keys(t, list(zip(K, [-0.015, -0.03, 0.005, -0.008, -0.01, -0.006, 0.0, 0.0]))),
        pitch=keys(t, list(zip(K, [1.5, 4.0, -3.0, -1.0, 0.0, 0.0, 0.0, 0.0]))),
        roll=3.0 * shake,
        chest=keys(t, list(zip(K, [1.0, 3.5, -2.5, 0.0, 0.0, 0.0, 0.0, 0.0]))),
        head=keys(t, list(zip(K, [24, 30, -22, -8, 0, 4, -2, 0]))),
        head_yaw=16.0 * shake,
        head_roll=9.0 * shake,
        tail=keys(t, list(zip(K, [16, 20, 5, -12, -8, 0, 4, 0]))),
        tail_wag=14 * shake,
    )


def plan(pairs_times):
    segs = []
    for t0, t1 in pairs_times:
        v = vback(t1)
        segs.append((t0, t1, (0.0, 0.10 * v / RETREAT), 0.045 if v > 0.5 else 0.025, 'step'))
    return segs


A = [(t, t + 0.07) for t in (0.08, 0.22, 0.36, 0.50, 0.64)] + [(0.80, 0.88)]
B = [(t, t + 0.07) for t in (0.15, 0.29, 0.43, 0.57, 0.71)] + [(0.88, 0.96)]
brace = [(0.0, 0.06, (0.0, -0.06), 0.0, 'drag')]
steps = {
    'FL': Steps(brace + plan(A), drift=drift),
    'HR': Steps(plan(A), drift=drift),
    'FR': Steps(brace + plan(B), drift=drift),
    'HL': Steps(plan(B), drift=drift),
}
# 最後の一歩はIdleの足位置（0,0）へ戻す
for leg in LEGS:
    s = steps[leg].segs[-1]
    steps[leg].segs[-1] = (s[0], s[1], (0.0, 0.0), 0.03, 'step')
reset()
b = Boar()
rep = build_oneshot(b, 'A_Boar_ChargeRecovery', N, body, steps)
rep.save()
b.export('A_Boar_ChargeRecovery', N)
if '--sheet' in sys.argv:
    setup_preview(b)
    contact_sheet(b, 'A_Boar_ChargeRecovery', [0, 4, 9, 14, 20, 26, 32, 38, 44, 50, 55, 60], views=('side', 'q34'), cols=6)
bpy.ops.wm.save_as_mainfile(filepath=ROOT + 'Blend/A_Boar_ChargeRecovery.blend')
