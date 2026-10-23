# ボア アニメーション原本

Blender 5.2 のスクリプトで、UE の既存Skeleton（Meshy_AI…_Skeleton）に合わせたアニメーションを生成する。
Mesh・Skeletonは作り直さず、`UEExport/` にUEから書き出したIdle・Walk・Meshを読み込んで使う。

| ファイル | 内容 |
|---|---|
| `UEExport/` | UEから書き出した原本（Boar_Mesh / A_Boar_Idle / AS_BoarWalk）。読み込み専用 |
| `Scripts/boar_common.py` | リグ読込・脚IK（2ボーン＋蹄/繋ぎ）・FBX書き出し |
| `Scripts/boar_motion.py` | ループ歩容・単発モーション・接地補正・検証レポート・コンタクトシート |
| `Scripts/build_*.py` | 各アニメーションの生成 |
| `Blend/` `FBX/` | 生成結果（FBXをUEの `/Game/Animation/Boar/` へImport） |
| `Preview/` | コンタクトシートと検証レポート（貫通・接地浮き・足滑り速度） |
| `Backup_20261023/` | 接続作業前の ABP_NormalBoar / BP_NormalBoar / BS_BoarLocomotion |

再生成：

```
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --python Scripts/build_telegraph.py -- --sheet
```

設計値（DataAssetの実値に合わせてある。値を変えた場合は該当スクリプトの速度も合わせる）：

- A_Boar_Trot 4.5m/s（BaseWalkSpeed 450）/ A_Boar_Run 6.75m/s（青の逃走 450×1.5）/ A_Boar_Charge 10m/s（ChargeSpeed 1000）
- A_Boar_AttackTelegraph 1.0秒（ABPが予兆時間に合わせて再生速度を調整）
- A_Boar_ChargeRecovery 1.0秒（後退 300cm/s・200cm に同期）
- 60fps・インプレース（ルートモーションなし）。ループは最終フレーム＝先頭フレーム
