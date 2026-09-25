# Stage 1 Visual Quality — 2026-09-25

対象: `/Game/Level/Test`。UE 5.8.2。Stage共通仕様のLow Polyルールを優先し、Stage 1空間仕様に照合。

## 変更

- 既存ChamferCubeを再利用した装飾Actor 77個を追加。`Stage01_Quality/Walls`、`Stage01_Quality/Facility`に整理。
- 外周: 柱28、シアンPanel24、Top Cap4、Base4。既存壁の位置とCollisionを維持し、白・青・シアンへ統一。
- 研究施設: 基礎、屋根の厚み、入口Frame、窓上下の構造、柱、側面窓、屋上設備等17部品を追加。既存建物のTransformを維持。
- `/Game/Stage01/Quality/Materials`にMaterial Instance 11個。`MI_S01_Quality_`のIvory/Blue/Cyan/Yellow/Glass/Stone/Machineと、`MI_S01_Nature_`のRock/Cliff/Leaves/Trunk。Stoneは未使用。
- 自然物は既存のFacet形状を維持。岩・崖・木のMaterialを高Roughnessへ調整。機械以外のMetallicは0。
- 新規77部品はNoCollision、Navigation影響なし。
- PIEで装飾床が元の床と衝突してBoarを妨げる問題を発見。既存の装飾床・通路・水面等34部品で、Mesh既定Collisionの使用を解除しNoCollisionを明示。元の歩行床、Spawn位置、AIは変更していない。

## 保存・検証

- Material再コンパイル、MapとMaterialの保存・再取得を実施。
- PIE: Enhanced InputのW入力による移動、SpaceBarでジャンプと着地を確認。記録したジャンプ上昇量248.89cm。10秒の記録でBoar 5体すべての位置変化を確認。
- Cage前への移動を確認。Cage前、橋の両側、高台へのNavMesh経路はいずれもvalid=true、partial=false。
- 計測スクリプト: `verify_stage01_pie.py`、結果: `verify_stage01_pie.json`。スクリプトは入力を解除し、計測用Callbackを終了時に解除する。
- Capture、Clear、AI、Spawn仕様、Player処理、StageConfig、Save、UIの実装変更なし。

## 未確認・制限

- 全通路・全接触角度での手動走行は未実施。NavMesh経路検証は全経路の物理的走破を保証しない。
- CaptureからClearまでの通し回帰テストは未実施。
- Notion本文の仕様は確認済み。埋め込みVisual Reference画像は取得できず、画像との厳密な比較は未実施。
