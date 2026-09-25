# Stage 1 Ground Visual Pass

対象: `/Game/Level/Test`。

正本: Notion「Codex / Unreal MCP 作業ルール」「ステージ」のLow Poly / Ground / Floor、「Lobby / Stage 1 空間デザイン仕様書」を確認。

既存のGrass床は薄いCube、土の周回路は薄いCubeの連結、Cage広場は薄いCylinder。既存の歩行床、通路、橋、Cage、Spawn、NavMeshは変更しない。

`Stage01_Ground`フォルダへ、既存のLow Poly岩を浅く変形した地表の盛り上がりと土の裾、その上のGrass / Bush / 小Rockを追加。道端4、水辺4、壁際1、既存岩周辺1の計10まとまり。均一Scatterは使用しない。岸には6個の低い湿土色の張り出し、周回路には6個の不規則な土の縁を追加。

追加Actor計78: 地表20、岸6、土の縁6、Grass 30、Bush 6、小Rock 10。既存Meshを再利用し、Mesh本体の編集・複製は行わない。

新規Material Instanceは `/Game/Stage01/Ground/Materials/MI_S01_Ground_` のTurf / TurfLight / DampSoil / DrySoil。既存の土用Materialを親として再利用し、Base ColorとRoughnessのみ調整。TextureやNormal Mapの追加なし。

追加装飾はNoCollision、bUseDefaultCollision=false、bCanEverAffectNavigation=false。視覚用の低い段差であり、歩行地形のCollisionを増やすものではない。

ユーザー確認: 通路とCageへの接近、橋の出入口、岸・草際での足元の埋まり、Boarの見つけやすさ、Playerカメラ距離での草の密度。PIE / Runtimeテストは依頼どおり実施しない。

保存結果: Mapと新規Material 4個はSave成功、再取得でdirty=false。追加78 ActorのNoCollision / Navigation影響なしを確認。保護対象14 ActorのTransform差分なし。追加Grass 30個はTurf Materialへ統一。
