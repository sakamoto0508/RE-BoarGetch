"""ボアのモーション組み立て：ループ歩容（Trot / Run / Charge）・接地補正・検証レポート・コンタクトシート。"""
import math, json
import bpy
import numpy as np
from mathutils import Vector
from boar_common import *

SIDE = {'FL': 1, 'FR': -1, 'HL': 1, 'HR': -1}


def ground_fix(b, tips, stance, iters=2, pitches=None):
    """蹄メッシュの最下点が地面に一致（立脚）／地面より上（遊脚）になるよう足先目標を補正する。"""
    pitches = pitches or {}
    for _ in range(iters):
        rep = b.contact_report()
        changed = False
        for leg in LEGS:
            err = rep[leg] / 100.0  # 地面からの高さ（m）
            if stance.get(leg) and abs(err) > 0.0008:
                tips[leg] = tips[leg] - Vector((0, 0, err))
                changed = True
            elif not stance.get(leg) and err < 0.003:
                tips[leg] = tips[leg] + Vector((0, 0, 0.003 - err))
                changed = True
        if not changed:
            break
        for leg in LEGS:
            hp, pp = pitches.get(leg, (0.0, 0.0))
            b.leg_ik(leg, tips[leg], hoof_pitch=hp, pastern_pitch=pp)
    return b.contact_report()


class Report:
    def __init__(self, name):
        self.name = name
        self.rows = []

    def add(self, f, b, stance, tips_body, reach):
        rep = b.contact_report()
        self.rows.append({'f': f, 'contact': {k: (round(v, 2) if isinstance(v, float) else v) for k, v in rep.items()},
                          'stance': {k: bool(v) for k, v in stance.items()},
                          'tip_y': {k: round(v, 4) for k, v in tips_body.items()}, 'reach_fail': reach})

    def summary(self, speed=None, fps=FPS):
        worst_pen = min(r['contact']['min'] for r in self.rows)
        head_clear = min(r['contact']['head'] for r in self.rows)
        pen_parts = sorted({r['contact']['lowest_part'] for r in self.rows if r['contact']['min'] < -0.3})
        stance_float = max((r['contact'][l] for r in self.rows for l in LEGS if r['stance'][l]), default=0)
        reach = sum(1 for r in self.rows for v in r['reach_fail'].values() if v)
        out = {'anim': self.name, 'frames': len(self.rows), 'worst_penetration_cm': round(worst_pen, 2), 'head_min_cm': round(head_clear, 2), 'penetrating_parts': pen_parts,
               'max_stance_float_cm': round(stance_float, 2), 'reach_fail_frames': reach}
        if speed is not None:
            # 立脚中の足先が体に対して後方へ動く速度（= ゲーム内でこの速度で動けば足滑り0）
            sl = []
            for l in LEGS:
                for a, c in zip(self.rows, self.rows[1:]):
                    if a['stance'][l] and c['stance'][l]:
                        sl.append((c['tip_y'][l] - a['tip_y'][l]) * fps)
            out['stance_speed_mps_mean'] = round(sum(sl) / max(1, len(sl)), 3)
            out['target_speed_mps'] = speed
        return out

    def save(self, extra=None):
        d = {'summary': self.summary(**(extra or {})), 'frames': self.rows}
        with open(ROOT + 'Preview/' + self.name + '_report.json', 'w') as f:
            json.dump(d, f, indent=1)
        print('REPORT', json.dumps(d['summary']))
        return d['summary']


def contact_sheet(b, name, frames, views=('side', 'q34'), size=(400, 300), cols=6):
    """指定フレームを各視点で描画し、1枚のシートにまとめる。"""
    scn = bpy.context.scene
    cam = scn.camera or setup_preview(b)
    scn.render.resolution_x, scn.render.resolution_y = size
    cams = {'side': ((3.6, -0.15, 0.38), (0, -0.12, 0.36)), 'q34': ((2.4, -2.7, 1.05), (0, -0.15, 0.32)),
            'front': ((0.4, -3.6, 0.6), (0, 0, 0.4)), 'top': ((0.01, -0.1, 4.2), (0, -0.1, 0))}
    tiles = []
    for v in views:
        aim_camera(cam, *cams[v])
        for f in frames:
            scn.frame_set(f)
            p = ROOT + 'Preview/_tile.png'
            render(p)
            img = bpy.data.images.load(p, check_existing=False)
            px = np.array(img.pixels[:]).reshape(size[1], size[0], 4)
            bpy.data.images.remove(img)
            tiles.append(px)
    rows = math.ceil(len(tiles) / cols)
    sheet = np.ones((rows * size[1], cols * size[0], 4))
    for i, t in enumerate(tiles):
        r, c = divmod(i, cols)
        # Blenderの画素は下から上へ並ぶため、シート上では上の行ほど下に置く
        sheet[(rows - 1 - r) * size[1]:(rows - r) * size[1], c * size[0]:(c + 1) * size[0]] = t
    out = bpy.data.images.new('sheet', cols * size[0], rows * size[1], alpha=True)
    out.pixels = sheet.flatten().tolist()
    out.filepath_raw = ROOT + 'Preview/' + name + '_sheet.png'
    out.file_format = 'PNG'
    out.save()
    print('SHEET', out.filepath_raw)


def build_loop(b, name, P):
    """解析的な周期歩容でループアニメを作る。P: パラメータ辞書。
    最終フレーム = 先頭フレーム（UEのループ規約）。"""
    N = P['frames']
    T = N / FPS
    v = P['speed']
    duty = P['duty']
    stride = v * duty * T
    rep = Report(name)
    b.new_action(name)
    for f in range(N + 1):
        t = f / FPS
        u = (f % N) / N
        b.apply_idle()
        bob = P['bob'](u)
        b.set_root(Vector((0, P.get('body_y', 0.0), P['height'] + bob)), pitch=P['pitch'](u), roll=P['roll'](u))
        b.rotate_local('chest', pitch=P['chest'](u))
        b.rotate_local('head', pitch=P['head'](u), yaw=P.get('head_yaw', lambda u: 0)(u))
        for i, n in enumerate(TAIL):
            b.rotate_local(n, pitch=P['tail'](u, i))
        tips, stance, pitches, tips_body, reach = {}, {}, {}, {}, {}
        for leg in LEGS:
            ph = P['phase'][leg]
            front = leg[0] == 'F'
            dy, dz, s = gait_foot(u - ph, duty, stride, P['lift_f'] if front else P['lift_h'],
                                  P['reach_f'] if front else P['reach_h'])
            base = b.idle_foot(leg)
            tip = Vector((base.x + SIDE[leg] * P.get('narrow', 0.0) * -1, base.y + dy, base.z + dz))
            stance[leg] = s < 0
            if s >= 0:
                curl = math.sin(math.pi * s) ** 1.2
                pitches[leg] = (P['hoof_curl'] * curl, P['pastern_curl'] * curl)
            else:
                # 立脚の終わり（蹴り出し）で蹄先が地面を押す
                push = ramp((u - ph) % 1.0, duty * 0.75, duty)
                pitches[leg] = (-P.get('toe_push', 0.0) * push, 0.0)
            tips[leg] = tip
            tips_body[leg] = tip.y
        for leg in LEGS:
            reach[leg] = b.leg_ik(leg, tips[leg], *pitches[leg])
        ground_fix(b, tips, stance, pitches=pitches)
        rep.add(f, b, stance, tips_body, reach)
        b.key(f)
    return rep, stride


# ---------------- 単発モーション用 ----------------
def keys(t, kv, ease=smooth):
    """[(時刻, 値), ...] をイーズ補間する。"""
    if t <= kv[0][0]:
        return kv[0][1]
    for (t0, v0), (t1, v1) in zip(kv, kv[1:]):
        if t <= t1:
            return lerp(v0, v1, ease((t - t0) / (t1 - t0)) if t1 > t0 else 1.0)
    return kv[-1][1]


def noise(t, freq, seed):
    """周期の異なる正弦の合成による簡易ノイズ（-1〜1）。"""
    return (math.sin(t * freq * 6.283 + seed) * 0.6 + math.sin(t * freq * 1.7 * 6.283 + seed * 2.3) * 0.4)


class Steps:
    """1脚の足先オフセット（Idle足位置からの相対, m）の時間変化。
    segs: (t0, t1, (dx, dy), lift, kind)  kind='step'（持ち上げて置く） / 'drag'（接地したまま引きずる）
    drift(t): 立脚中に足先が体に対して後方(+Y)へ流れる速度[m/s]（体が前進しているとき正）。"""

    def __init__(self, segs=(), drift=None, curl=(70, 30), start=(0.0, 0.0)):
        self.start = Vector(start)
        self.segs = sorted(segs, key=lambda s: s[0])
        self.drift = drift
        self.curl = curl

    def sample(self, times):
        out = []
        pos = self.start.copy()
        dt = times[1] - times[0] if len(times) > 1 else 0
        for t in times:
            seg = next((s for s in self.segs if s[0] <= t < s[1]), None)
            if seg is None:
                if self.drift:
                    pos = pos + Vector((0.0, self.drift(t) * dt))
                done = [s for s in self.segs if s[1] <= t]
                out.append((pos.copy(), 0.0, True, (0.0, 0.0)))
                continue
            t0, t1, tgt, lift, kind = seg
            if not hasattr(seg, 'start'):
                pass
            key = (t0, t1)
            if key not in getattr(self, '_starts', {}):
                self.__dict__.setdefault('_starts', {})[key] = pos.copy()
            p0 = self._starts[key]
            s = (t - t0) / (t1 - t0)
            if kind == 'drag':
                e = smooth(s)
                pos = p0.lerp(Vector(tgt), e)
                out.append((pos.copy(), 0.0, True, (-12.0 * math.sin(math.pi * s), 0.0)))
            else:
                e = smoother(s)
                pos = p0.lerp(Vector(tgt), e)
                z = lift * math.sin(math.pi * s) ** 0.9
                c = math.sin(math.pi * s) ** 1.2
                out.append((pos.copy(), z, False, (self.curl[0] * c * min(1.0, lift / 0.08), self.curl[1] * c * min(1.0, lift / 0.08))))
        return out


def build_oneshot(b, name, N, body, steps, extra=None, ik=True):
    """body(t) -> dict(dy, dz, pitch, roll, yaw, chest, chest_yaw, head, head_yaw, head_roll, tail, tail_wag)
    steps: {leg: Steps}"""
    times = [f / FPS for f in range(N + 1)]
    tracks = {leg: steps[leg].sample(times) for leg in LEGS}
    rep = Report(name)
    b.new_action(name)
    for f, t in enumerate(times):
        B = body(t)
        b.apply_idle()
        b.set_root(Vector((B.get('dx', 0.0), B.get('dy', 0.0), B.get('dz', 0.0))), pitch=B.get('pitch', 0.0),
                   roll=B.get('roll', 0.0), yaw=B.get('yaw', 0.0))
        b.rotate_local('chest', pitch=B.get('chest', 0.0), yaw=B.get('chest_yaw', 0.0), roll=B.get('chest_roll', 0.0))
        b.rotate_local('head', pitch=B.get('head', 0.0), yaw=B.get('head_yaw', 0.0), roll=B.get('head_roll', 0.0))
        if B.get('jaw'):
            b.rotate_local('headend', pitch=B['jaw'])
        for i, n in enumerate(TAIL):
            b.rotate_local(n, pitch=B.get('tail', 0.0) * (1.0 if i == 0 else 0.5),
                           yaw=B.get('tail_wag', 0.0) * (0.4 + 0.3 * i))
        tips, stance, pitches, tips_body, reach = {}, {}, {}, {}, {}
        for leg in LEGS:
            off, z, st, pc = tracks[leg][f]
            base = b.idle_foot(leg)
            tips[leg] = Vector((base.x + off.x * SIDE[leg], base.y + off.y, base.z + z))
            stance[leg] = st
            pitches[leg] = pc
            tips_body[leg] = tips[leg].y
        if extra:
            extra(b, t, tips, stance, pitches)
        if ik:
            for leg in LEGS:
                reach[leg] = b.leg_ik(leg, tips[leg], *pitches[leg])
            ground_fix(b, tips, stance, pitches=pitches)
        else:
            # Additive用：脚はIdleのまま体に追従させ、差分に脚の変形を含めない
            reach = {leg: False for leg in LEGS}
        rep.add(f, b, stance, tips_body, reach)
        b.key(f)
    return rep
