"""A_Boar_Charge：突進ループ（頭を低く、強い蹴り、飛翔期のある横ギャロップ）。ChargeSpeed 1000cm/s で足滑り0。
Run: blender -b --python build_charge.py [-- --sheet]"""
import sys; sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/Boar/Scripts')
from boar_motion import *
S = lambda u, p=0.0: math.sin(2 * math.pi * (u - p))
C = lambda u, p=0.0: math.cos(2 * math.pi * (u - p))
P = dict(
    frames=14, speed=10.0, duty=0.18,
    phase={'HL': 0.0, 'HR': 0.1, 'FL': 0.42, 'FR': 0.52},
    reach_f=-0.05, reach_h=0.0, lift_f=0.11, lift_h=0.09, narrow=0.015,
    hoof_curl=85, pastern_curl=40, toe_push=25,
    height=-0.012, body_y=0.0,
    bob=lambda u: 0.032 * C(u, 0.86) - 0.004 * C(2 * u, 0.2),
    pitch=lambda u: 1.5 + 2.5 * S(u, 0.12),
    roll=lambda u: 1.6 * S(u, 0.25),
    chest=lambda u: 1.0 + 2.0 * S(u, 0.35),
    head=lambda u: 24.0 - 0.6 * (2.5 * S(u, 0.12) + 2.0 * S(u, 0.35)),
    tail=lambda u, i: (16 if i == 0 else 9) + 5 * S(u, 0.1 + i * 0.12),
)
reset()
b = Boar()
rep, stride = build_loop(b, 'A_Boar_Charge', P)
print('STRIDE', stride)
rep.save({'speed': P['speed']})
b.export('A_Boar_Charge', P['frames'])
if '--sheet' in sys.argv:
    setup_preview(b)
    contact_sheet(b, 'A_Boar_Charge', [0, 2, 4, 6, 8, 10, 12, 13], views=('side',), cols=4)
bpy.ops.wm.save_as_mainfile(filepath=ROOT + 'Blend/A_Boar_Charge.blend')
