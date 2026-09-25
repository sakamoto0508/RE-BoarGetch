# Stage 1 ブロックアウト

2026-09-24。最新の[空間デザイン仕様書](https://www.notion.so/3e4b6c88752581d79bc2f319d1bfe773)に基づき、Unreal MCPで配置。ローポリ風の方向性に合わせた基本形状による試作で、完成アートではない。

## 開く場所

- Level: `/Game/Level/Test`（既存Stage01の遷移先を維持）
- Outliner: `Stage01_Blockout` 以下。追加Actorは `S01_BO_` で始まる60個。
- 仮Material: `/Game/Stage01/Blockout/Materials` に8個のMaterial Instance。既存 `M_FlatCol` を再利用。
- Lobbyは構造・PlayerStart・入口を読み取りのみ。今回の制作対象はStage 1。

## 配置内容

- 既存Cage (120, -1510, 0) を軸とする楕円の周回Pathと東西南北の接続路。
- 左（-X）に池、小川、滝の仮形状と木橋。左奥に風車。
- 右（+X）に高さ約2.66mの高台と見張り台。南北2本の約15度の坂を配置。
- 奥（-Y）に白・青の研究施設の仮シルエット。
- 外周に落下防止用の仮壁。地形の基本形状はCube/Cylinder/Coneを利用。
- 水面と土のPathは見た目用でCollisionなし。水中・泳ぎ・ダメージは未実装。水面下の既存床は通行できる。
- 風車は静止したランドマーク。植栽や細かいFence等は移動・追跡確認後の工程。

## 維持したもの

既存Actorの削除・改名・置換なし。Cage、PlayerStart、BoarSpawnPoint 5個はTransformを再取得し変更なしを確認。PlayerStartの向きも維持したため、開始直後にCageが正面へ来る構図にはしていない。

StageConfigはStage01、通常Boar 5体、目標収容数3。既存の捕獲・Cage・Clearコード、入力、UI、個体ID未登録の方針を維持。

NavMeshBoundsVolumeのみ、新地形へ対応するため範囲を拡張した。現在のBoundsはX=-3850..3850、Y=-4050..1250、Z=-200..1200cm。

## 確認

- `BP_Cage` / `BP_BoarSpawnPoint` / `BP_BoarGameMode`: warnings_as_errorsでCompile成功。
- Build Paths実行完了。範囲拡張時にNavMeshタイル数変更による再作成ログあり。
- Map Check: 0 Error / 0 Warning。
- Levelと8個のMaterial InstanceをSaveし、再取得でdirty=false。
- エディタ俯瞰で配置確認。PIE・Standalone・移動やAIの実行テストはユーザー方針に従い未実施。
- C++変更なし。

## 次のユーザーテスト

1. 既存のLobbyからStage01へ入り、Boar 5体とHUD表示を確認。
2. 檻周囲を一周し、橋の両端、高台の両坂、見張り台の段差を移動する。
3. Boarが坂と橋で追跡できるか、Cageへ複数方向から戻れるか確認。
4. 捕獲・現在収容数3でクリア・Result遷移を確認。
5. カメラの遮蔽、開始時の見通し、移動距離を調整候補として確認。

## 素材候補（2026-09-24に公開ページを確認）

### 第一候補: River Forest - RAD Low Poly / Fab

https://www.fab.com/listings/e8bf2cd3-1828-4860-8d0c-8ad6ec253cba

無料表示、UE形式。60モデル、草・地形・水のMaterial、River Spline Toolを含む。明るいローポリ研究フィールドの自然部分を揃える候補。UE 5.8での互換性は未確認。まず木・岩・水辺素材として導入し、既存Stageの導線に合わせて配置する。配布側は水面の泡にGenerate Mesh Distance Fields、Landscapeの変形Collisionに追加設定が必要と説明しているため、採用機能に応じて確認する。

### 軽量な代替: Kenney Nature Kit

https://kenney.nl/assets/nature-kit

無料・CC0、330ファイル。木・岩・植生を揃える候補。ユーザー側でUEへインポート後、Scale・Material・Collisionを確認して配置する。

今回はどちらもダウンロード・購入・インポートしていない。Fabの所有ライブラリは未確認。既に所有するローポリの風車・橋・木柵・干し草・木箱・研究施設があれば、プロジェクトへ導入されたものを優先して再利用する。
