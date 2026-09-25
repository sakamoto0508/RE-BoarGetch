# Stage 1 インポート素材の配置

2026-09-24。ユーザーが導入したRiver ForestをUnreal MCPで配置。

## 変更

- `/Game/Level/Test` の既存池・小川・滝の岩Actorは名前を維持し、StaticMeshを実素材へ差し替え。
- `/Game/InportAssets/Meshes` の解決済みMeshと、`/Game/InportAssets/LPRiverForest/Materials` のMaterialを利用。`/Game/LPRiverForest/Meshes` の参照もこちらへ解決されていたため、整理・移動・削除は行っていない。
- 新規60 Actor: 樹木11、岩8、遠景の山3、橋の木柵12、低木12、高台の崖2、草12。
- 追加分はOutliner `Stage01_Art` 以下。池・小川・滝の岩も対応するWaterフォルダへ整理。
- 草地と水面に専用MIを作成: `/Game/Stage01/Art/Materials/MI_Stage01_Grass`、`MI_Stage01_ShallowWater`。
- 草地は頂点色のない基本形状でも白くならないようPaint色を調整。水面はWaterHeightStrength=2、WaterHeightCorrection=0として、元素材の大きな変形を浅い水辺向けに抑制。水辺は引き続き視覚表現のみで泳ぎ判定なし。
- 高台本体にインポートした岩Materialを適用。主要な床・坂・橋の当たり判定を維持。

## 維持・未完了

Cage・PlayerStart・5つのSpawnPoint・StageConfig・ゲーム処理・UIは維持。対応素材のない風車・見張り台・研究施設・橋の床は仮形状を継続使用。外周の仮壁も落下防止として維持。

移動とBoar追跡の実行確認後に、植生の密度や岩の位置を調整する。完成アートとしての仕上げはまだ残る。

## 確認

- 使用するM_LowPoly / M_LowPolyWater / M_LowPolyGrassを再Compile: 成功。グラフ変更なし。再Compileでdirtyになった親Materialも保存。
- 木・木柵・崖のCollision形状を読取確認。
- Build Paths完了、Map Checkは0 Error / 0 Warning。
- Level、専用MI、再CompileしたMaterialを保存しdirty=falseを再取得。
- エディタの俯瞰表示で配置・色を確認。PIE・Standalone等の実行テストは未実施。
- ユーザー側では橋の出入口・欄干、高台両側の坂、木と岩の周囲のBoar追跡、3体収容からResultまでを確認する。
