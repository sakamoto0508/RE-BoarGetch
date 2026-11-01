"""発見リアクション（Additive）と青ボアの疲労。
A_Boar_Alert           : Normal/青。プレイヤーに気付いた瞬間、頭を跳ね上げて小さく飛び上がり尾が立つ（0.45秒）。
A_Boar_AlertAggressive : 赤。頭を低く構えて鼻を鳴らすように振り、体を沈める（0.5秒）。
  ※2本ともFrame0 = Idle の差分（UE側でLocal Space Additive / 参照=Frame0）。脚は動かさず、走り・歩きの上へ重ねる。
A_Boar_Tired           : スタミナ切れ。脚を開いて頭を下げ、速い呼吸で体が上下する（1.6秒ループ）。
A_Boar_TiredRecover    : 回復。頭を上げて体を振り、姿勢を立て直す（0.5秒）。
Run: blender -b --python build_reactions.py [-- --sheet]"""
import sys; sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/Boar/Scripts')
from boar_motion import *
SHEET = '--sheet' in sys.argv
still = lambda: {leg: Steps() for leg in LEGS}

def run(name, N, body, steps, ik=True, frames=None):
    reset()
    b = Boar()
    rep = build_oneshot(b, name, N, body, steps, ik=ik)
    rep.save()
    b.export(name, N)
    if SHEET:
        setup_preview(b)
        contact_sheet(b, name, frames or list(range(0, N + 1, max(1, N // 7))), views=('side', 'q34'), cols=8)
    bpy.ops.wm.save_as_mainfile(filepath=ROOT + 'Blend/' + name + '.blend')

# ---------- Alert（Normal・青）----------
def alert(t):
    k = [0.0, 0.06, 0.14, 0.26, 0.45]
    return dict(dz=keys(t, list(zip(k, [0, 0.025, 0.012, 0.004, 0]))),
                pitch=keys(t, list(zip(k, [0, -4, -2, -1, 0]))),
                chest=keys(t, list(zip(k, [0, -3, -2, -0.5, 0]))),
                head=keys(t, list(zip(k, [0, -24, -18, -8, 0]))),
                head_roll=keys(t, list(zip(k, [0, 5, -3, 1, 0]))),
                tail=keys(t, list(zip(k, [0, 30, 26, 12, 0]))),
                tail_wag=6 * math.sin(t * 6.283 * 9) * (1 - ramp(t, 0.1, 0.4)))
run('A_Boar_Alert', 27, alert, still(), ik=False)

# ---------- AlertAggressive（赤）----------
def alert_red(t):
    k = [0.0, 0.08, 0.2, 0.34, 0.5]
    snort = math.sin(6.283 * 7 * t) * bump(t, 0.12, 0.2, 0.36)
    # 脚に影響するルート・胸は動かさず（Additiveで脚が地面に沈むのを防ぐ）、頭と尾で構えを作る
    return dict(head=keys(t, list(zip(k, [0, 24, 28, 12, 0]))) + 3 * snort,
                head_yaw=6 * snort, head_roll=-4 * snort,
                tail=keys(t, list(zip(k, [0, 28, 30, 15, 0]))))
run('A_Boar_AlertAggressive', 30, alert_red, still(), ik=False)

# ---------- Tired（ループ 1.6秒 / 呼吸4回）----------
TP = 1.6
def tired(t):
    br = math.sin(6.283 * 4 / TP * t)            # 速い呼吸
    sw = math.sin(6.283 / TP * t)                # ゆっくりした揺れ
    return dict(dz=-0.03 + 0.007 * br, dy=0.01,
                pitch=3.0 + 0.8 * br, roll=1.5 * sw,
                chest=3.0 + 1.6 * math.sin(6.283 * 4 / TP * t + 0.6),
                head=26 + 3.0 * math.sin(6.283 * 4 / TP * t + 1.1) + 2 * sw,
                head_roll=4 * sw, head_yaw=3 * math.sin(6.283 / TP * t + 1.3),
                jaw=-(7 + 4 * math.sin(6.283 * 4 / TP * t + 0.3)),
                tail=-20 + 3 * sw)
splay = {'FL': (0.035, -0.025), 'FR': (0.035, -0.025), 'HL': (0.02, 0.02), 'HR': (0.02, 0.02)}
run('A_Boar_Tired', 96, tired, {leg: Steps(start=splay[leg]) for leg in LEGS}, frames=[0, 6, 12, 18, 24, 48, 72, 96])

# ---------- TiredRecover（0.5秒）----------
def recover(t):
    T0 = tired(0.0)
    up = ramp(t, 0.0, 0.18)
    shake = math.sin(6.283 * 6 * (t - 0.12)) * bump(t, 0.12, 0.22, 0.42)
    return dict(dz=lerp(T0['dz'], 0.0, up) + 0.006 * bump(t, 0.1, 0.2, 0.3), dy=lerp(T0['dy'], 0.0, up),
                pitch=lerp(T0['pitch'], -1.5, up) * (1 - ramp(t, 0.3, 0.5)), roll=7 * shake,
                chest=lerp(T0['chest'], -1.0, up) * (1 - ramp(t, 0.3, 0.5)),
                head=keys(t, [(0, T0['head']), (0.16, -16), (0.32, -4), (0.5, 0)]),
                head_roll=12 * shake, head_yaw=8 * shake,
                jaw=lerp(T0['jaw'], 0.0, ramp(t, 0.0, 0.15)),
                tail=keys(t, [(0, T0['tail']), (0.18, 25), (0.5, 0)]),
                tail_wag=20 * shake)
run('A_Boar_TiredRecover', 30, recover,
    {leg: Steps([(0.22 + (0.05 if leg[0] == 'H' else 0), 0.36 + (0.05 if leg[0] == 'H' else 0), (0.0, 0.0), 0.035, 'step')],
                start=splay[leg]) for leg in LEGS})
