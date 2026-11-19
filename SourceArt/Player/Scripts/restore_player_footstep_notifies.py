"""Player歩行・走行アニメーションの足音Notify（AnimNotify_BoarFootstep）を、記録済みの配置どおりに復元する。

`Content/InportAssets/` はGit管理外のため、外部Playerアセットを導入・再インポートした後にこのスクリプトを手動で実行する。
- 接地時刻は推定し直さない。下の FOOTSTEPS に記録した時刻・足Boneをそのまま使う（2026-11-19に配置しPIE確認待ちの値）。
- 先に4本すべてを検査し、1本でも想定と違えば何も変更せずに停止する（Asset欠落・Skeleton違い・長さ違い）。
- 既に同じNotify（同じClass・時刻・FootBone）があれば追加しない。何度実行しても各Sequenceは2個のまま。
- 追加するのは `Footstep` トラックのAnimNotify_BoarFootstepだけ。他のNotify・Track・Curve・Root Motion・Sequence設定・Skeletonは変更しない。
- 記録と違うAnimNotify_BoarFootstepが既にある場合（手動で調整した等）は削除・上書きせず、報告して停止する。

実行（Git Bashでプロジェクトのルートから。Editorは閉じておく。スクリプトは絶対パスで渡す）:
    Tools/Verification/run_ue_py.sh "$PWD/SourceArt/Player/Scripts/restore_player_footstep_notifies.py"
変更せずに確認だけする場合:
    BOAR_FOOTSTEP_DRY_RUN=1 Tools/Verification/run_ue_py.sh "$PWD/SourceArt/Player/Scripts/restore_player_footstep_notifies.py"
Editorを開いている場合は、Output LogのCmd欄で `py "<このファイルの絶対パス>"` でも実行できる。
結果は Saved/Verification/restore_player_footstep_notifies.json に出力する。
"""
import json
import os

import unreal

NOTIFY_CLASS_PATH = "/Script/REBoarGetch.AnimNotify_BoarFootstep"
TRACK = "Footstep"
SKELETON = "/Game/InportAssets/Player/GreatSwordSlash_UE_Skeleton.GreatSwordSlash_UE_Skeleton"
TIME_TOLERANCE = 0.002   # 秒。同一Notifyとみなす時刻の差
LENGTH_TOLERANCE = 0.01  # 割合。再インポートで長さが変わっていたら記録時刻が無効なので停止する

# 対象フォルダ。通常は変更しない（検証用に一時コピー先を指す場合だけ環境変数で上書きする）。
ANIM_ROOT = os.environ.get("BOAR_FOOTSTEP_ANIM_ROOT", "/Game/InportAssets/Animation").rstrip("/")

# アニメーション名 → (記録時の長さ秒, [(発火時刻秒, 足Bone)])。時刻は2026-11-19につま先＋足首の接地判定で求めた値。
FOOTSTEPS = {f"{ANIM_ROOT}/{name}": value for name, value in {
    "A_Player_Walk": (1.3667, [(0.3758, "foot_l"), (1.0933, "foot_r")]),
    "A_Player_Run": (0.5333, [(0.1689, "foot_l"), (0.4356, "foot_r")]),
    "A_Player_Walk_Net": (1.3667, [(0.3758, "foot_l"), (1.0933, "foot_r")]),
    "A_Player_Run_Net": (0.5333, [(0.1689, "foot_l"), (0.4356, "foot_r")]),
}.items()}

DRY_RUN = os.environ.get("BOAR_FOOTSTEP_DRY_RUN", "") not in ("", "0")
AL = unreal.AnimationLibrary
OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Verification", "restore_player_footstep_notifies.json")


def footstep_events(anim, notify_class):
    """このClassのNotifyを (時刻, FootBone) で返す。トラックは問わない（別トラックへ移した場合も重複扱い）。"""
    result = []
    for event in AL.get_animation_notify_events(anim):
        notify = event.notify
        if notify and notify.get_class() == notify_class:
            result.append((AL.get_anim_notify_event_trigger_time(event), str(notify.get_editor_property("FootBone"))))
    return result


def matches(existing, time, bone):
    return any(abs(t - time) <= TIME_TOLERANCE and b == bone for t, b in existing)


def write_report(report):
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=1)


def main():
    report = {"dry_run": DRY_RUN, "status": "", "problems": [], "assets": {}}

    notify_class = unreal.load_class(None, NOTIFY_CLASS_PATH)
    if not notify_class:
        report["problems"].append(f"{NOTIFY_CLASS_PATH} が見つかりません。REBoarGetchEditorをビルドしてから実行してください。")

    # ---- 1. 事前検査（ここで1件でも問題があれば何も変更しない） ----
    anims = {}
    for path, (length, steps) in FOOTSTEPS.items():
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            report["problems"].append(f"{path} がありません。外部Playerアセット（Content/InportAssets/Animation）を導入してください。")
            continue
        anim = unreal.load_asset(path)
        if not isinstance(anim, unreal.AnimSequence):
            report["problems"].append(f"{path} がAnimSequenceではありません（{anim.get_class().get_name() if anim else 'None'}）。")
            continue
        skeleton = anim.get_editor_property("skeleton")
        skeleton_path = skeleton.get_path_name() if skeleton else "None"
        if skeleton_path != SKELETON:
            report["problems"].append(f"{path} のSkeletonが想定と違います: {skeleton_path}（想定 {SKELETON}）。")
            continue
        actual = anim.get_play_length()
        if abs(actual - length) > length * LENGTH_TOLERANCE:
            report["problems"].append(f"{path} の長さが記録と違います: {actual:.4f}秒（記録 {length:.4f}秒）。再インポートで尺が変わった場合は時刻を測り直してください。")
            continue
        if notify_class:
            unexpected = [(round(t, 4), b) for t, b in footstep_events(anim, notify_class) if not any(
                abs(t - time) <= TIME_TOLERANCE and b == bone for time, bone in steps)]
            if unexpected:
                report["problems"].append(f"{path} に記録と違う足音Notifyがあります: {unexpected}。手動調整を上書きしないため停止します。")
                continue
        anims[path] = anim

    if report["problems"]:
        report["status"] = "stopped: nothing changed"
        write_report(report)
        for problem in report["problems"]:
            unreal.log_error("[FootstepRestore] " + problem)
        raise RuntimeError("[FootstepRestore] 事前検査で問題が見つかったため、何も変更せずに停止しました。詳細: " + OUT)

    # ---- 2. 不足分だけ追加 ----
    for path, anim in anims.items():
        steps = FOOTSTEPS[path][1]
        existing = footstep_events(anim, notify_class)
        missing = [(t, b) for t, b in steps if not matches(existing, t, b)]
        entry = {"already_present": len(steps) - len(missing), "added": [], "saved": False}

        if missing and not DRY_RUN:
            if not AL.is_valid_anim_notify_track_name(anim, TRACK):
                AL.add_animation_notify_track(anim, TRACK)
            for time, bone in missing:
                notify = AL.add_animation_notify_event(anim, TRACK, time, notify_class)
                notify.set_editor_property("FootBone", unreal.Name(bone))
                entry["added"].append([time, bone])
            entry["saved"] = unreal.EditorAssetLibrary.save_loaded_asset(anim, True)
        elif missing:
            entry["would_add"] = [[t, b] for t, b in missing]

        # ---- 3. 事後確認：記録した全Notifyがちょうど1個ずつある ----
        final = footstep_events(anim, notify_class)
        entry["final"] = [[round(t, 4), b] for t, b in sorted(final)]
        if not DRY_RUN:
            for time, bone in steps:
                count = sum(1 for t, b in final if abs(t - time) <= TIME_TOLERANCE and b == bone)
                if count != 1:
                    report["problems"].append(f"{path}: {bone}@{time} が {count} 個です（1個のはず）。")
            if len(final) != len(steps):
                report["problems"].append(f"{path}: 足音Notifyが {len(final)} 個です（{len(steps)} 個のはず）。")
        report["assets"][path] = entry

    report["status"] = "dry run" if DRY_RUN else ("ok" if not report["problems"] else "verify failed")
    write_report(report)
    for path, entry in report["assets"].items():
        unreal.log_warning(f"[FootstepRestore] {path}: present={entry['already_present']} added={len(entry['added'])} final={entry['final']}")
    if report["problems"]:
        raise RuntimeError("[FootstepRestore] 事後確認に失敗しました: " + "; ".join(report["problems"]))
    unreal.log_warning(f"[FootstepRestore] COMPLETE ({report['status']})")


main()
