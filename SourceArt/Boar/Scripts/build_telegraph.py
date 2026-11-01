"""A_Boar_AttackTelegraph：突進前のため（1.0秒）。
0.00-0.10 気付き：頭を上げて身構える → 0.08-0.30 前脚を外・前へ開いて踏ん張り、頭を低く →
0.38-0.66 右前脚で地面を掻く（突進の合図） → 0.66-0.90 後脚に体重を溜め震える → 0.90-1.00 前へ重心を投げ出す（Charge開始姿勢へ）。
ゲーム側では予兆時間（DataAsset AttackTelegraphDuration）に合わせて再生速度を調整するため、モーション長は1.0秒基準。
Run: blender -b --python build_telegraph.py [-- --sheet]"""
import sys; sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/Boar/Scripts')
from boar_motion import *
N = 60
K = [0.0, 0.08, 0.22, 0.38, 0.55, 0.72, 0.88, 0.95, 1.0]


def body(t):
    J = ramp(t, 0.66, 0.74) * (1 - ramp(t, 0.9, 0.95))  # 震え
    paw = bump(t, 0.40, 0.50, 0.66)
    return dict(
        dy=keys(t, list(zip(K, [0, -0.01, 0.015, 0.035, 0.04, 0.06, 0.068, 0.05, -0.01]))) + 0.003 * J * noise(t, 17, 1),
        dz=keys(t, list(zip(K, [0, 0.004, -0.006, -0.02, -0.022, -0.038, -0.045, -0.035, -0.015]))) + 0.003 * J * noise(t, 19, 4),
        pitch=keys(t, list(zip(K, [0, -1.0, 1.5, 3, 3, 4, 4.5, 3.5, 1.5]))),
        roll=2.5 * paw,
        chest=keys(t, list(zip(K, [0, -2, 1, 2, 2, 2.5, 3, 2, 1]))),
        head=keys(t, list(zip(K, [0, -18, 8, 20, 22, 27, 30, 28, 24]))) + 1.2 * J * noise(t, 15, 2) - 6 * paw,
        head_roll=-4 * paw,
        head_yaw=2.0 * J * noise(t, 11, 7),
        tail=keys(t, list(zip(K, [0, 18, 10, 18, 22, 32, 36, 26, 16]))),
        tail_wag=8 * math.sin(t * 6.283 * 6) * (1 - ramp(t, 0.3, 0.45)) + 10 * J * noise(t, 9, 3),
    )


steps = {
    'FL': Steps([(0.10, 0.22, (0.05, -0.05), 0.06, 'step')]),
    'FR': Steps([(0.16, 0.28, (0.05, -0.05), 0.06, 'step'),
                 (0.40, 0.50, (0.06, -0.11), 0.09, 'step'),     # 地面掻き：前へ
                 (0.50, 0.62, (0.06, -0.01), 0.0, 'drag'),      # 引きずって後ろへ
                 (0.62, 0.72, (0.05, -0.05), 0.05, 'step')]),   # 踏み直し
    'HL': Steps([(0.30, 0.40, (0.02, 0.03), 0.03, 'step')]),
    'HR': Steps([(0.34, 0.44, (0.02, 0.03), 0.03, 'step')]),
}
reset()
b = Boar()
rep = build_oneshot(b, 'A_Boar_AttackTelegraph', N, body, steps)
rep.save()
b.export('A_Boar_AttackTelegraph', N)
if '--sheet' in sys.argv:
    setup_preview(b)
    contact_sheet(b, 'A_Boar_AttackTelegraph', [0, 6, 13, 20, 27, 33, 38, 44, 50, 56, 60], views=('side', 'q34'), cols=6)
bpy.ops.wm.save_as_mainfile(filepath=ROOT + 'Blend/A_Boar_AttackTelegraph.blend')
