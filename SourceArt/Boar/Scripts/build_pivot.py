"""逃走中の切り返し（AKI-152）。ゲーム側 ABoarBase の EscapeTurnPhase に対応する。
A_Boar_PivotBrace   : 減速→踏ん張り→停止（0.6秒、単発。ABPは最終フレームで保持）。
                      DataAsset Normal.EscapeTurn の Decelerate 0.25 + Brace 0.2 + Hold 0.15 秒に合わせた長さ。
                      0.00-0.25 前脚を前へ突っ張って滑り、腰を落として体重を後ろへ → 0.25-0.45 止まり切って体が揺れる →
                      0.45-0.60 低い姿勢で構えたまま静止。
A_Boar_PivotTurn_R  : その場で右（上から見て時計回り）へ旋回するループ（0.8秒 / 2歩周期）。
A_Boar_PivotTurn_L  : 同じく左。
  旋回そのものはゲーム側でActorを回す（PivotTurnRateDegreesPerSecond = 150度/秒）。アニメは足踏みだけで、
  立脚中の蹄は体に対して旋回と逆向きに円弧を描いて流れる（150度/秒で回れば足滑り0）。ルートは回さない（インプレース）。
  逃走開始の素早い旋回（StartTurn 540度/秒）は同じループを再生速度を上げて使う。
Run: blender -b --python build_pivot.py [-- --sheet]"""
import sys; sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/Boar/Scripts')
from boar_motion import *

SHEET = '--sheet' in sys.argv
TURN_RATE = 150.0  # 度/秒（DataAsset Normal.EscapeTurn.PivotTurnRateDegreesPerSecond）


def finish(b, name, N, rep, frames, views=('side', 'q34', 'top')):
    rep.save()
    b.export(name, N)
    if SHEET:
        setup_preview(b)
        contact_sheet(b, name, frames, views=views, cols=len(frames))
    bpy.ops.wm.save_as_mainfile(filepath=ROOT + 'Blend/' + name + '.blend')


# ---------------- PivotBrace ----------------
NB = 36
KB = [0.0, 0.08, 0.18, 0.25, 0.32, 0.40, 0.48, 0.6]


def brace_body(t):
    shake = math.sin(6.283 * 9 * (t - 0.25)) * bump(t, 0.25, 0.30, 0.42)
    return dict(
        dy=keys(t, list(zip(KB, [0.0, 0.015, 0.035, 0.045, 0.05, 0.045, 0.04, 0.04]))),
        dz=keys(t, list(zip(KB, [0.0, -0.008, -0.022, -0.032, -0.036, -0.032, -0.03, -0.03]))) + 0.002 * shake,
        pitch=keys(t, list(zip(KB, [0.0, -2.0, -4.0, -4.5, -3.0, -2.0, -1.5, -1.5]))),
        roll=1.2 * shake,
        chest=keys(t, list(zip(KB, [0.0, -2.0, -3.5, -3.0, -1.5, -1.0, -0.5, -0.5]))),
        head=keys(t, list(zip(KB, [0.0, -10.0, -16.0, -14.0, -6.0, -2.0, 0.0, 0.0]))) + 3.0 * shake,
        head_roll=2.0 * shake,
        tail=keys(t, list(zip(KB, [0.0, 18.0, 30.0, 32.0, 24.0, 20.0, 18.0, 18.0]))),
        tail_wag=10.0 * shake,
    )


brace_steps = {
    # 前脚：前へ突っ張ったまま地面を擦って止まる
    'FL': Steps([(0.02, 0.10, (0.03, -0.07), 0.035, 'step'), (0.10, 0.26, (0.035, -0.10), 0.0, 'drag')]),
    'FR': Steps([(0.05, 0.13, (0.03, -0.07), 0.035, 'step'), (0.13, 0.26, (0.035, -0.10), 0.0, 'drag')]),
    # 後脚：腰を落として少し後ろ・外へ開く
    'HL': Steps([(0.12, 0.22, (0.025, 0.03), 0.03, 'step')]),
    'HR': Steps([(0.16, 0.26, (0.025, 0.03), 0.03, 'step')]),
}
reset()
b = Boar()
rep = build_oneshot(b, 'A_Boar_PivotBrace', NB, brace_body, brace_steps)
finish(b, 'A_Boar_PivotBrace', NB, rep, [0, 5, 10, 15, 20, 25, 30, 36], views=('side', 'q34'))


# ---------------- PivotTurn（ループ） ----------------
NT = 36
T = NT / FPS          # 0.6秒
P = T / 2             # 1歩の周期 0.3秒
SWING = 0.11
# 対角の2脚ずつ交互に踏み替える
PHASE = {"FL": 0.02, "HR": 0.02, "FR": 0.17, "HL": 0.17}


class TurnSteps:
    """その場旋回の足先（Idle足位置からの相対, m）。立脚中は体に対して旋回と逆向きに回り、
    遊脚で旋回方向へ半歩先の位置へ置き直す。1ループ前から計算して先頭と末尾を一致させる。"""

    def __init__(self, base, center, omega, phase, lift):
        self.base, self.center, self.omega, self.phase, self.lift = base, center, omega, phase, lift
        stance = P - SWING
        self.target = self._abs_to_off(self._rotate(base, -omega * stance / 2))

    def _rotate(self, p, ang):
        c = self.center
        x, y = p.x - c.x, p.y - c.y
        ca, sa = math.cos(ang), math.sin(ang)
        return Vector((c.x + x * ca - y * sa, c.y + x * sa + y * ca, p.z))

    def _abs_to_off(self, p):
        return Vector((p.x - self.base.x, p.y - self.base.y))

    def _off_to_abs(self, off):
        return Vector((self.base.x + off.x, self.base.y + off.y, self.base.z))

    def _swing(self, t):
        u = (t - self.phase) % P
        return u / SWING if u < SWING else None

    def sample(self, times):
        dt = times[1] - times[0]
        pre = [t - T for t in times[:-1]]
        pos = Vector((0.0, 0.0))
        out = []
        start = None
        for i, t in enumerate(pre + times):
            s = self._swing(t)
            if s is None:
                pos = self._abs_to_off(self._rotate(self._off_to_abs(pos), self.omega * dt))
                if start is not None:
                    start = None
                if i >= len(pre):
                    out.append((pos.copy(), 0.0, True, (0.0, 0.0)))
            else:
                if start is None:
                    start = pos.copy()
                e = smoother(s)
                pos = start.lerp(self.target, e)
                z = self.lift * math.sin(math.pi * s) ** 0.9
                c = math.sin(math.pi * s) ** 1.2
                if i >= len(pre):
                    out.append((pos.copy(), z, False, (55.0 * c, 25.0 * c)))
        return out


def turn_body(sign):
    # sign: +1 = 右旋回。体の yaw+ は左向きなので、旋回方向へ向く量は -sign。
    def body(t):
        u = t / T
        bob = 0.006 * math.sin(6.283 * 2 * u)
        return dict(
            dy=0.01, dz=-0.018 + bob,
            pitch=-0.5 + 0.6 * math.sin(6.283 * 2 * u + 0.8),
            roll=sign * 2.0 + 0.8 * math.sin(6.283 * 2 * u),
            chest=-1.0,
            chest_yaw=-sign * 6.0,
            head=-6.0 + 2.0 * math.sin(6.283 * 2 * u + 1.2),
            head_yaw=-sign * 14.0,
            head_roll=sign * 3.0,
            tail=10.0, tail_wag=sign * 8.0 + 4.0 * math.sin(6.283 * 2 * u),
        )
    return body


for name, sign in (('A_Boar_PivotTurn_R', 1.0), ('A_Boar_PivotTurn_L', -1.0)):
    reset()
    b = Boar()
    b.apply_idle()
    bases = {leg: b.idle_foot(leg) for leg in LEGS}
    cy = sum(v.y for v in bases.values()) / 4.0
    fy = (bases['FL'].y + bases['FR'].y) / 2.0
    # 回転中心を体の中央から少し前脚寄りにし、前脚の立脚の振れ幅（IKの届く範囲）を抑える
    center = Vector((0.0, lerp(cy, fy, 0.35), 0.0))
    # 右旋回（上から時計回り）では、地面に固定された点は体から見て反時計回り（+角）に流れる
    omega = sign * math.radians(TURN_RATE)
    # build_oneshotは off.x に SIDE を掛けるため、左右で符号を合わせたラッパーを渡す
    steps = {}
    for leg in LEGS:
        ts = TurnSteps(bases[leg], center, omega, PHASE[leg], 0.05 if leg[0] == 'F' else 0.04)

        class Wrap:
            def __init__(self, inner, side):
                self.inner, self.side = inner, side

            def sample(self, times):
                return [(Vector((p.x * self.side, p.y)), z, st, pc) for p, z, st, pc in self.inner.sample(times)]
        steps[leg] = Wrap(ts, SIDE[leg])
    rep = build_oneshot(b, name, NT, turn_body(sign), steps)
    finish(b, name, NT, rep, [0, 5, 9, 14, 18, 23, 27, 32, 36])
