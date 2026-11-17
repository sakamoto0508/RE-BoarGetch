"""Normal Boarの切り返し：急ブレーキ→踏ん張り→ひねって旋回→蹴り出し（AKI-172）。ゲーム側 ABoarBase の EscapeTurnPhase に対応する。
DataAsset Normal.EscapeTurn：Decelerate 0.25秒（400→0cm/s）＋Brace 0.2秒（停止）＋Hold 0.8秒（停止して旋回）→ Accelerate 0.5秒（15%→100%）。

A_Boar_PivotBrace   : 0.45秒・単発。Decelerate＋Braceの区間（AM_Boar_PivotBraceは最終フレームで保持）。
    0.00-0.07 走りの姿勢から前脚を前へ大きく踏み出す（FLの接地0.07秒、FRの接地0.095秒＝土煙のNotify）
    0.07-0.25 前脚を突っ張ったまま地面を擦り、体が前脚の上へ前のめりに沈む。頭は慣性で遅れて下がり、後脚は遅れて体の下へ寄る
    0.25      ゲーム側の停止（StopMovementImmediately）。ここで最も前へ沈んだ姿勢
    0.25-0.45 停止の衝撃で揺れ、重心が前脚から4本の支持脚の上へ戻る。低く構えた姿勢で終わる
A_Boar_PivotSwing_R : Hold（停止して旋回）中のループ（0.6秒・2歩周期）。150度/秒で回したとき立脚の蹄が地面に対して止まる
A_Boar_PivotSwing_L   （回転中心＝Actor原点。ゲーム側はEscapePivotTurnRate/150の再生速度で再生する）。
    頭→肩（胸）→腰の順に旋回方向へ向き（ひねり）、前脚は低く小刻みに・後脚は大きく持ち上げて回り込む。
A_Boar_PivotPushOff : 0.30秒・単発。Accelerate開始。60cm/sから毎秒680cm/sで加速する体に合わせ、
    立脚の蹄を加速に合わせて後方へ流す（足滑り0）。後脚で地面を蹴り、前のめりに伸び上がって走りへ戻る。
Run: blender -b --python build_brake.py [-- --sheet]"""
import sys; sys.path.append('C:/UnrealGames/REBoarGetch/SourceArt/Boar/Scripts')
from boar_motion import *

SHEET = '--sheet' in sys.argv
TURN_RATE = 150.0     # 度/秒。ABoarBase::EscapeTurnAnimationRate（BP_NormalBoar 150）と一致させる
DECEL = 0.25          # DataAsset Normal.EscapeTurn.DecelerateDuration
RUN_SPEED = 4.0       # m/s。Normalの逃走速度 150×2.666667
ACC_START, ACC_RATE = 0.6, 6.8   # m/s, m/s²。PivotInitialSpeedRatio 0.15×400、(400-60)/0.5


def finish(b, name, N, rep, frames, views=('side', 'q34')):
    rep.save()
    b.export(name, N)
    if SHEET:
        setup_preview(b)
        contact_sheet(b, name, frames, views=views, cols=len(frames))
    bpy.ops.wm.save_as_mainfile(filepath=ROOT + 'Blend/' + name + '.blend')


def shake(t, t0, f=8.0, decay=0.16):
    """t0から減衰する揺れ（停止の衝撃）。"""
    if t < t0:
        return 0.0
    return math.sin(6.283 * f * (t - t0)) * math.exp(-(t - t0) / decay)


# ---------------- PivotBrace（急ブレーキ＋踏ん張り） ----------------
NB = 27  # 0.45秒
KB = [0.00, 0.04, 0.08, 0.14, 0.20, 0.25, 0.29, 0.34, 0.40, 0.45]


def brace_body(t):
    s = shake(t, 0.25)
    return dict(
        # +dy = 体が後ろへ。前脚の上へ乗り出し（-）→停止の反動で支持脚の上へ戻る（+）
        dy=keys(t, list(zip(KB, [-0.010, -0.012, -0.016, -0.022, -0.028, -0.030, -0.010, 0.012, 0.014, 0.012]))),
        dz=keys(t, list(zip(KB, [0.002, 0.000, -0.012, -0.026, -0.040, -0.046, -0.040, -0.036, -0.038, -0.038]))) + 0.004 * s,
        # pitch+ = 頭が下がる。前脚に体重が乗って前へ傾き、停止後に起き上がる
        pitch=keys(t, list(zip(KB, [2.0, 3.0, 4.0, 6.0, 7.5, 8.0, 5.0, 2.0, 2.5, 2.5]))) + 1.2 * s,
        roll=1.6 * s,
        # 胸は体より遅れて前へ倒れ、遅れて戻る
        chest=keys(t, list(zip(KB, [0.5, 0.5, 1.5, 3.5, 6.0, 7.0, 7.0, 2.0, 0.5, 0.5]))),
        # 頭は慣性で最後まで前へ振られる（停止直後に最も下がる）
        head=keys(t, list(zip(KB, [-4.0, -3.0, -1.0, 3.0, 7.0, 9.0, 12.0, 7.0, 4.0, 5.0]))) + 3.0 * s,
        head_roll=2.5 * s,
        tail=keys(t, list(zip(KB, [22.0, 24.0, 28.0, 34.0, 40.0, 44.0, 40.0, 30.0, 26.0, 26.0]))),
        tail_wag=12.0 * s,
    )


brace_steps = {
    # 前脚：走りの振り出しから前へ大きく踏み出して着地し、突っ張ったまま擦って止まる（止まった後は動かさない＝支点）
    'FL': Steps([(0.00, 0.07, (0.025, -0.165), 0.055, 'step'), (0.07, DECEL, (0.03, -0.085), 0.0, 'drag')],
                start=(0.0, -0.03)),
    'FR': Steps([(0.00, 0.095, (0.03, -0.150), 0.065, 'step'), (0.095, DECEL, (0.035, -0.075), 0.0, 'drag')],
                start=(0.0, 0.02)),
    # 後脚：走りで後ろへ蹴り出した位置から遅れて着地し、少し擦ってから体の下へ踏み込む
    'HL': Steps([(0.00, 0.10, (0.015, 0.040), 0.040, 'step'), (0.10, 0.15, (0.015, 0.055), 0.0, 'drag'),
                 (0.16, 0.27, (0.03, 0.015), 0.045, 'step')], start=(0.0, 0.055)),
    'HR': Steps([(0.00, 0.12, (0.015, 0.035), 0.035, 'step'), (0.12, 0.17, (0.015, 0.050), 0.0, 'drag'),
                 (0.19, 0.30, (0.03, 0.020), 0.040, 'step')], start=(0.0, 0.045)),
}
reset()
b = Boar()
rep = build_oneshot(b, 'A_Boar_PivotBrace', NB, brace_body, brace_steps)
finish(b, 'A_Boar_PivotBrace', NB, rep, [0, 4, 6, 9, 12, 15, 18, 21, 27])


# ---------------- PivotSwing（停止して旋回するループ） ----------------
NT = 36
T = NT / FPS          # 0.6秒
P = T / 2             # 1歩の周期 0.3秒
SWING = {'F': 0.09, 'H': 0.13}   # 前脚は短く小刻みに、後脚は長く持ち上げて回り込む
PHASE = {"FL": 0.02, "HR": 0.05, "FR": 0.17, "HL": 0.20}


class SwingSteps:
    """その場旋回の足先（Idle足位置からの相対, m）。立脚中はActor原点回りに旋回と逆向きへ回る（地面に対して静止）。
    遊脚で旋回方向へ置き直す。1ループ前から計算して先頭と末尾を一致させる。"""

    def __init__(self, base, omega, phase, lift, swing, lead):
        self.base, self.omega, self.phase, self.lift, self.swing = base, omega, phase, lift, swing
        stance = P - swing
        # 立脚の中央で基準位置に来るよう、着地点を半分先へ置く。leadは前脚を旋回方向へ少し先に置く量（ひねり）
        self.target = self._abs_to_off(self._rotate(base, -omega * stance / 2 - lead))

    @staticmethod
    def _rotate(p, ang):
        ca, sa = math.cos(ang), math.sin(ang)
        return Vector((p.x * ca - p.y * sa, p.x * sa + p.y * ca, p.z))

    def _abs_to_off(self, p):
        return Vector((p.x - self.base.x, p.y - self.base.y))

    def _off_to_abs(self, off):
        return Vector((self.base.x + off.x, self.base.y + off.y, self.base.z))

    def _swing(self, t):
        u = (t - self.phase) % P
        return u / self.swing if u < self.swing else None

    def sample(self, times):
        dt = times[1] - times[0]
        pre = [t - T for t in times[:-1]]
        pos = Vector((0.0, 0.0))
        out, start = [], None
        for i, t in enumerate(pre + times):
            s = self._swing(t)
            if s is None:
                pos = self._abs_to_off(self._rotate(self._off_to_abs(pos), self.omega * dt))
                start = None
                if i >= len(pre):
                    out.append((pos.copy(), 0.0, True, (0.0, 0.0)))
            else:
                if start is None:
                    start = pos.copy()
                pos = start.lerp(self.target, smoother(s))
                z = self.lift * math.sin(math.pi * s) ** 0.9
                c = math.sin(math.pi * s) ** 1.2
                if i >= len(pre):
                    out.append((pos.copy(), z, False, (60.0 * c, 28.0 * c)))
        return out


def swing_body(sign):
    # sign: +1 = 右旋回。体のyaw+は左向きなので、旋回方向へ向く量は -sign。
    def body(t):
        u = t / T
        return dict(
            dy=0.008, dz=-0.034 + 0.005 * math.sin(6.283 * 2 * u + 0.6),
            pitch=2.0 + 0.8 * math.sin(6.283 * 2 * u + 0.8),
            roll=sign * 3.5 + 0.8 * math.sin(6.283 * 2 * u),   # 旋回の内側へ体を傾ける
            yaw=sign * 3.0,                                     # 腰は旋回に遅れる（ひねり）
            chest=1.0, chest_yaw=-sign * 11.0,                  # 肩が先に回る
            head=6.0 + 2.0 * math.sin(6.283 * 2 * u + 1.2),
            head_yaw=-sign * 20.0, head_roll=sign * 4.0,        # 頭が最も先に旋回先を向く
            tail=18.0, tail_wag=sign * 14.0 + 5.0 * math.sin(6.283 * 2 * u),
        )
    return body


for name, sign in (('A_Boar_PivotSwing_R', 1.0), ('A_Boar_PivotSwing_L', -1.0)):
    reset()
    b = Boar()
    b.apply_idle()
    bases = {leg: b.idle_foot(leg) for leg in LEGS}
    # 右旋回（上から時計回り）では、地面に固定された点は体から見て反時計回り（+角）に流れる
    omega = sign * math.radians(TURN_RATE)
    steps = {}
    for leg in LEGS:
        front = leg[0] == 'F'
        ss = SwingSteps(bases[leg], omega, PHASE[leg], 0.035 if front else 0.065, SWING[leg[0]],
                        sign * math.radians(4.0) if front else 0.0)

        class Wrap:
            def __init__(self, inner, side):
                self.inner, self.side = inner, side

            def sample(self, times):
                return [(Vector((p.x * self.side, p.y)), z, st, pc) for p, z, st, pc in self.inner.sample(times)]
        steps[leg] = Wrap(ss, SIDE[leg])
    rep = build_oneshot(b, name, NT, swing_body(sign), steps)
    finish(b, name, NT, rep, [0, 5, 9, 14, 18, 23, 27, 32, 36], views=('side', 'q34', 'top'))


# ---------------- PivotPushOff（蹴り出し） ----------------
NP = 18  # 0.30秒
KP = [0.00, 0.05, 0.10, 0.15, 0.22, 0.30]


def push_drift(t):
    return ACC_START + ACC_RATE * t   # 立脚の蹄が体に対して後方へ流れる速度（＝体の速度）


def push_body(t):
    return dict(
        dy=keys(t, list(zip(KP, [0.012, 0.018, 0.0, -0.018, -0.012, -0.006]))),
        # 一度沈んで溜め、後脚で蹴って伸び上がる
        dz=keys(t, list(zip(KP, [-0.038, -0.046, -0.030, -0.006, 0.006, 0.004]))),
        pitch=keys(t, list(zip(KP, [2.5, -2.0, -1.0, 4.0, 3.0, 2.0]))),
        chest=keys(t, list(zip(KP, [0.5, -1.0, 1.0, 3.0, 2.0, 1.5]))),
        head=keys(t, list(zip(KP, [6.0, 3.0, -2.0, -4.0, 0.0, 2.0]))),
        tail=keys(t, list(zip(KP, [26.0, 30.0, 34.0, 30.0, 26.0, 24.0]))),
    )


push_steps = {
    # 後脚：体の下で踏ん張ったまま加速に合わせて後方へ蹴り（立脚）、伸び切ってから前へ振り戻す
    'HL': Steps([(0.13, 0.30, (0.01, -0.05), 0.075, 'step')], drift=push_drift, start=(0.03, -0.045)),
    'HR': Steps([(0.15, 0.30, (0.01, -0.035), 0.070, 'step')], drift=push_drift, start=(0.03, -0.035)),
    # 前脚：先に持ち上げて前へ伸ばし、着地して一歩流してから次の振り出しへ
    'FL': Steps([(0.02, 0.11, (0.02, -0.11), 0.065, 'step'), (0.21, 0.30, (0.0, -0.06), 0.070, 'step')],
                drift=push_drift, start=(0.03, -0.085)),
    'FR': Steps([(0.04, 0.13, (0.02, -0.10), 0.065, 'step'), (0.23, 0.30, (0.0, -0.03), 0.060, 'step')],
                drift=push_drift, start=(0.035, -0.075)),
}
reset()
b = Boar()
rep = build_oneshot(b, 'A_Boar_PivotPushOff', NP, push_body, push_steps)
finish(b, 'A_Boar_PivotPushOff', NP, rep, [0, 3, 6, 9, 12, 15, 18])
print('BRAKE_DONE')
