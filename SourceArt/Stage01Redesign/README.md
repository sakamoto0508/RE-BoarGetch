# Stage01 Redesign

Stage 1 を作り直したもの。草原と研究施設がある箱庭型のステージで、チュートリアルも兼ねる。
`/Game/Level/Stage1` は変更していない。新しいLevelは `/Game/Level/L_Stage01`。

## 配置（UE座標, cm）
- 開始地点 (120, 950) yaw -90。前方 2.4 m に檻 `BP_Cage` (120, -1510) があり、位置は Stage1 のまま。
- 左手前：池。左奥：小川、木橋 (-2640, -2000)、小滝、風車。
- 右：低い丘（高さ 2.4 m、坂は約 20°以下）と見張り台 (3120, -1650)。見張り台にはコイン候補 `Stage01_CoinSpawn_001` を置いた。
- 奥：研究施設のフェンスとゲート。その外に施設本体、ドーム、アンテナ。
- 道：檻を中心に一周するリング道と、東西南北の4本の道。開始地点から池と丘へ向かう小道もある。
- ボアの出現地点：ID `Stage01_Spawn_001`〜`005` はそのままで、位置だけ変更した。DA_TestStageConfig の 001〜003 に対応する。

## ファイル
| 種類 | 場所 |
|---|---|
| レイアウト定義（単一ソース） | `Scripts/s01r_layout.py`（地形の高さ関数、道、水辺、出現地点）, `Scripts/s01r_dressing.py`（全メッシュの配置） |
| Blenderでの生成 | `Scripts/build_assets.py` → `Blend/S01R_Assets.blend`, `FBX/*.fbx`, `Screenshots/blender_assets/` |
| Blenderでのレイアウトプレビュー | `Scripts/layout_preview.py` → `Blend/S01R_LayoutPreview.blend`, `Screenshots/blender_layout/` |
| UEへのインポート | `Scripts/ue_import_assets.py`（ヘッドレス。インポート先 `/Game/InportAssets/Stage01Redesign/Meshes`、`Materials`） |
| UEでのLevel構築 | `Scripts/ue_build_level.py`（GUIエディタ。Stage1 をテンプレートに作成し、配置、NavMesh再ビルド、保存まで行う） |
| 検証 | `Scripts/ue_gameplay_check.py`（PIE）, `Scripts/ue_nav_diag.py`, `Scripts/capture_level.py`。結果は `Logs/gameplay_check.json` |

再生成の手順：
1. `blender -b --factory-startup --python Scripts/build_assets.py`
2. `sh Scripts/ue_cmd.sh <abs>/Scripts/ue_import_assets.py ue_import`
3. `sh Scripts/ue_gui.sh <abs>/Scripts/ue_build_level.py ue_build -S01Quit`
4. `sh Scripts/ue_gui.sh <abs>/Scripts/ue_gameplay_check.py gameplay_check -S01Quit`

スクリプトのパスは Windows 形式の絶対パス（`C:/...`）で渡す。

## 注意
- Blender では UE の座標系で作成し、`MB.v()` で一度だけ変換している（UE (x,y,z) cm → Blender (x/100, -y/100, z/100) m）。FBX は Blender の既定設定で書き出すと、UE で元の座標に戻る。
- マテリアルスロット名と既存MIの対応表は `ue_import_assets.py` の `UE_MATS` にある。木材は M_FlatCol から作った子MIを使っている。MI_TreeWood は頂点カラーが必要なので使っていない。
- NavMesh：エディタは、アセットのコンパイルが終わるまで `AsyncLoadLock` で Build() を無視する。そのため `ue_build_level.py` は池・橋・丘の3点が NavMesh に乗るまで再ビルドを繰り返す。
- PIE検証の前に `Saved/SaveGames` をスクラッチにバックアップし、終了時に元へ戻す。
- コリジョン：Collision Preset が BlockAll でも、単純コリジョン形状がないメッシュはキャラクターを止めない。自作の小物には `ue_fix_collision.py`（GUIエディタ必須）でボックスを付けてある。既存パックの岩・崖・樽などは `Meshes/Reused/` に複製し、そちらにコリジョンを付けて Level の配置を差し替えた（元のアセットは変更していない）。対応表は `s01r_dressing.REUSED`。
- Level の名前はエディタ上で `L_Stage01` に変更された。`ue_build_level.py` で作り直すと S01R の配置がすべて置き直され、手作業の変更が消える。既存の Level を直すときは `ue_patch_collision_level.py` のように差分だけ当てる。
