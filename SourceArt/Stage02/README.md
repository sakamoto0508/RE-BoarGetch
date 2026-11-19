# Stage02（段丘牧場）

Stage 2 の地形と初期 Visual（AKI-174 Phase A）。中央の谷底に檻があり、北の高い尾根から飛び降りて先回りする丘陵牧場。
Level は `/Game/Level/L_Stage02`（GameMode Override は `BP_GM_Stage02`、StageConfig は `/Game/DataAssets/StageConfig/DA_Stage02StageConfig`）。

## ボアの Spawn（AKI-175）
`BP_GM_Stage02.StageConfig = DA_Stage02StageConfig`（StageId `Stage02`、TargetCaptureCount 4）。ボアは Level に直接置かず、
既存の `BoarStageSpawner` が StageConfig の `BoarSpawnDefinitions` から `SpawnPointId` で生成する。SpawnPoint はカプセル中心の高さ（地面 +60）。

| BoarUniqueId | BoarClass | SpawnPointId | 位置 (x, y, z) | 場所 |
|---|---|---|---|---|
| Stage02_Boar_001 | BP_NormalBoar | Stage02_Spawn_001 | (1300, -3600, 450) | 西の丘（川の西岸、林の中の道） |
| Stage02_Boar_002 | BP_NormalBoar | Stage02_Spawn_002 | (-1700, -1700, 82) | 南西の中段（開始地点から約 19 m） |
| Stage02_Boar_003 | BP_RedBoar | Stage02_Spawn_003 | (900, 2700, 317) | 東の段丘の草地（半径 5 m 内に障害物なし） |
| Stage02_Boar_004 | BP_BlueBoar | Stage02_Spawn_004 | (3250, 2800, 733) | 北東の尾根（近道 B の東） |

設定は `Scripts/ue_setup_gameplay.py`（ヘッドレス、エディタを閉じて実行。同じ ID の SpawnPoint だけ置き直す）、確認は `Scripts/ue_verify_gameplay.py`、上面図は `Scripts/capture_spawns.py`（`Screenshots/aki175/`）。

> **L_Stage02 にはエディタでの手作業の変更が入っている（2026-11-19：橋の移動、ボラード・ガイドビーコン・近道の標識の削除、岩 3 個の移動）。**
> `ue_build_level.py` で作り直すとこれが消える。以後の変更は `ue_patch_level.py` のような差分パッチで行う。

## 配置（UE座標, cm, +X=北 / +Y=東, 檻の地面 = 0）
- プレイ範囲 75 m（南北）× 90 m（東西）。外側は見えない境界壁（Camera / Visibility は無視）と、範囲外の丘・森・遠景の山。
- 中央：谷底の草地（0）。檻 `BP_Cage` (300, 250)。足元に小さな研究用の足場と木箱。
- 南：牧場の台地（+70）。開始地点 (-3050, -350)、檻の方を向く。納屋、母屋、荷車、井戸、放牧柵。
- 水面：池は水位固定（`POND_WATER_Z` = -25、縁で一番低い所より下）で、池底をそこから掘る。川の水面の端は岸の中に 15 cm 埋める（`build_terrain.build_water`、横から見て浮かない）。
- 西：南へ流れる S 字の川と池、林、丘（+420）。**川と池には入れない**（見えないブロッカー `Stage02/WaterBlock`）。渡れるのは橋 (170, -2730) だけ。尾根の西端からも西側へ行ける。
- 東：段丘（+260）。尾根の下は +330 の段。広い草地と風車の小山。
- 北：尾根（+650）と見張り台。谷側は崖（ボアは登れない）。西端と東端に登り道。
  - 近道 A（`SHORTCUTS['NW']`, y=-1300）：尾根 → 崖の途中の岩棚（+330）→ 谷。1 回の落下は約 3.2 m。
  - 近道 B（`SHORTCUTS['NE']`, y=+2000）：尾根 → 東の段丘の段（+330）。
- 花は道を避けて、木や岩の近くや草地に小さなかたまりで置く（`s02_dressing.natural_flowers`）。

## ファイル
| 種類 | 場所 |
|---|---|
| レイアウト定義（単一ソース） | `Scripts/s02_layout.py`（高さ関数、道、川、橋、近道、ランドマーク） |
| 配置定義 | `Scripts/s02_dressing.py`（既存アセットの配置リスト。手作業で消した物はここからも外した） |
| Blender での地形生成 | `Scripts/build_terrain.py` → `FBX/SM_S02_*.fbx`, `Blend/S02_Terrain.blend`, `Screenshots/blender/`, `Logs/terrain_stats.json` |
| UE インポート | `Scripts/ue_import_assets.py`（ヘッドレス。`/Game/Stage02/Meshes`。マテリアルは Stage 1 の既存 MI） |
| UE Level 差分パッチ | `Scripts/ue_patch_level.py`（GUI エディタ。地形の再インポートと小物の接地補正、花の置き直し、水のブロッカー、NavMesh 再ビルド、保存） |
| UE Level 全体構築（初回用） | `Scripts/ue_build_level.py`（手作業の変更が消えるので今は使わない） |
| 撮影 | `Scripts/capture_level.py` + `cams_before.json` / `cams_after.json`（GUI エディタ。保存しない） |
| 検証 | `Scripts/ue_verify.py`、`Scripts/ue_dump_level.py`（ヘッドレス。`Logs/`） |

地形を変えるときの手順（エディタを閉じてから。L_Stage02 を開いたままだと保存できない）：
1. `blender -b --factory-startup --python Scripts/build_terrain.py`
2. `UnrealEditor <uproject> /Engine/Maps/Entry -ExecutePythonScript=<abs>/Scripts/ue_patch_level.py -RenderOffscreen -S02Quit`
3. `UnrealEditor <uproject> /Engine/Maps/Entry -ExecutePythonScript=<abs>/Scripts/capture_level.py -RenderOffscreen -S02Tag=after -S02Quit`

## 注意
- 岩・樽などコリジョンが効かないパックのメッシュは、Stage 1 が作った `Stage01Redesign/Meshes/Reused` の複製を使う（`s02_dressing.REUSED`）。元のアセットは変更しない。
- 範囲外の森・遠景の山・草花はコリジョンなし。
- 地形は 150 cm グリッドなので、3 m 未満の段差は NavMesh 上で歩ける斜面になることがある。ボアを止めたい崖は経路長で確認する。
- NavMesh：エディタはアセットのコンパイル中は Build() を無視するので、プローブ点が NavMesh に乗るまで再ビルドを繰り返す。
- `Logs/` と `Screenshots/` は Git 管理外。

## Stage Select ジオラマ（`Diorama/`）
Stage Select 中央に表示する展示専用ミニチュア。実ステージは読み込まず、`ABoarStagePreviewActor`（Stage 1 と同じ SceneCapture 方式・同じ正射影 Camera）で撮影する。
- `Diorama/build_diorama.py`（Blender）：`s02_layout.py` の高さ関数を 150 cm グリッドで再サンプル（水平 0.088 倍、高さはさらに 2 倍に誇張）。プレイ範囲の角丸輪郭で切り抜き、側面に土の厚みを付ける。川は幅を広げた青い帯。檻・檻の足場・風車・見張り台・橋は自前の FBX 原本（`SourceArt/Cage`、`SourceArt/Stage01Redesign`）を拡大して配置し、木・納屋・母屋・柵・近道の岩は `s02_dressing.py` の位置に簡易形状で置く。全 Mesh が頂点カラー＋1 スロット（FBX は linear で出力。UE の VertexColor ノードは sRGB 変換をしないため）。
- `Diorama/ue_apply_diorama.py`（ヘッドレス、エディタを閉じて実行）：`/Game/UI/StagePreview/Meshes/SM_S02_Diorama_{Island,Props}`、`M_Preview_VertexColor`（Stage 1 の `M_Preview_*` と同じ擬似ライティング）、`BP_Stage02_Diorama`（Yaw 45 で北を奥へ、Z -87 で手前下端を Stage 1 と同じ画面位置へ）、`DA_Stage02StageConfig`（表示名・PreviewActorClass）、`BP_StageEntrance_Stage01.StageCatalog` に Stage02 を追加。
- `Diorama/ue_verify_diorama.py`（ヘッドレス・読み取り専用）、`Diorama/capture_dioramas.py`（GUI・実 GPU・PIE なし）、`Diorama/build_select_mockups.js`（Stage Select の合成モック）。
- 台座は Mesh に含めない。WBP の共通ホログラム台座（`T_SS_Pedestal`）を Stage 1 と共用する。
