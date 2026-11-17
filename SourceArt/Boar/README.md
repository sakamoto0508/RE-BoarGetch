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
| `Backup_20261023/` | 接続作業前のローカルバックアップ（Git追跡対象外）。共有用の旧版はGit履歴を参照 |

再生成：

```
"C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --python Scripts/build_telegraph.py -- --sheet
```

設計値（DataAssetの実値に合わせてある。値を変えた場合は該当スクリプトの速度も合わせる）：

- A_Boar_Trot 4.5m/s（BaseWalkSpeed 450）/ A_Boar_Run 6.75m/s（青の逃走 450×1.5）/ A_Boar_Charge 10m/s（ChargeSpeed 1000）
- A_Boar_AttackTelegraph 1.0秒（ABPが予兆時間に合わせて再生速度を調整）
- A_Boar_ChargeRecovery 1.0秒（後退 300cm/s・200cm に同期）
- A_Boar_PivotTurn_L・R 0.6秒ループ（逃走開始の旋回用。旋回150度/秒で足滑り0。`build_pivot.py`、AKI-152）
- Normalの切り返し（`build_brake.py`、AKI-172）。DataAsset Normal.EscapeTurn（減速0.25＋踏ん張り0.2＋停止旋回0.8＋加速0.5秒、逃走400cm/s）に合わせてある：
  - A_Boar_PivotBrace 0.45秒：走りの姿勢から前脚を踏み出して突っ張る（FL 0.07秒・FR 0.095秒に接地＝土煙Notify）→ 0.25秒で停止して前のめり → 支持脚の上へ戻る
  - A_Boar_PivotSwing_L・R 0.6秒ループ：停止旋回。頭→肩→腰の順に旋回先へ向き、Actor原点回りに150度/秒で足滑り0（ゲーム側が実際の旋回速度÷150の再生速度で再生）
  - A_Boar_PivotPushOff 0.30秒：60cm/sから680cm/s²で加速する体に合わせて立脚を流し、後脚で蹴って走りへ戻る
  - 土煙の粒子Mesh `SM_BoarDustPuff`（`build_dust_mesh.py`）
- 60fps・インプレース（ルートモーションなし）。ループは最終フレーム＝先頭フレーム
