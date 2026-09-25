# Stage 1 床構造改修

対象Map: `/Game/Level/Test`。保存済み。

- `/Game/Stage01/GroundStructure/Meshes/SM_S01_*`に専用Low Poly Mesh 10個を追加。Cage基礎・舗装2種・Warning Ring・Cyan Guide・土道の切り口・草地テラス2種・岸・高台の裾。新規MaterialはMI_S01_FacilityFloor 1個。その他Materialは既存を再利用。
- Cageは16角形の薄い基礎、12分割舗装、黄色の分割Warning Ring、四方向のシアン案内面で施設床化。Cage本体は変更しない。
- 周回路の内外へ連続した草地の肩を追加。草側は約10～14cm、既存土道側は約2.7cm。幅と外側輪郭を少し変化させ、四方向の交差部は開口。直線接続路6区間も薄い土の切り口と草地の肩を追加。
- 岸は水面付近から16cmまでの湿土色の面と草地への斜面。橋と小川への接続を開ける。高台の裾は最大44cmの岩面から草地へ接続し、坂口・Cage側の経路は開ける。
- 前回追加した道端3か所の19装飾Actorを新しい境界へ移動。草は増やしていない。
- 新規10 ActorはNoCollision / bUseDefaultCollision=false / bCanEverAffectNavigation=false。視覚用地形で、歩行Collisionは既存のまま。保護対象14 ActorのTransform差分なし。
- Mesh 10個・Material 1個・MapのSave成功、再取得でdirtyなし。Editorの俯瞰・近景表示を確認。PIE / Runtimeは実施していない。

ユーザー確認: Cageへの接近、周回路と接続路の通行、橋・坂口、境界での足元の埋まりや浮き、Cameraからの段差の見え方。

生成中のUV未設定によるEditor終了は、UV追加後に再起動して復旧。前回保存済み78 Actorを再確認した。面の向きも修正済み。生成スクリプトは `build_stage01_ground_structure.py`。このスクリプトの再実行は今回の10 Meshのみを再構築し、Level Actorの作成や移動は含まない。
