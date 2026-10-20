# RetroRender（RE-BoarGetch）

PS1 / N64 / PS2 / CRT の見た目を、エディタとPIEで切り替えるプロジェクトプラグインです。
Fabの「Retro Render」の公開仕様を参考にした独自実装で、製品のコードやアセットは使っていません。

## 構成

| 層 | 実体 | 役割 |
|---|---|---|
| Runtime | `URetroRenderComponent` | 1つのunbound PostProcessでMIDを適用。`MPC_Retro`と`MPC_UnlitStrength`に形状系の値を書き込む。AA・Lumen反射・ポストFX・雲・低解像度描画の切り替えを、OFFや登録解除時に復元 |
| Runtime | `ARetroRenderActor` | レベルに置く設定アクタ（エディタのビューポートでもプレビュー可） |
| Runtime | `ARetroCompareActor` | 比較・撮影用。Ctrl+Alt+F7のUMG、`+CRT`ボタン、自動撮影API |
| Runtime | `FRetroRenderParameters` / `DA_RetroPresets` | プリセットの値。`GetRetroParameterDescriptors()`でマテリアル名・MPC名・UI範囲を一元管理 |
| Editor | Window/Tools ›「Retro Render」タブ | Look（プリセット・全パラメータの即時反映・CRT重ね・レベル追加・既定値保存・OFF）、Textures、Meshes |
| Editor | `URetroRenderEditorLibrary` | Python／Blueprintから同じ処理を呼ぶ関数群（テクスチャ、Bake、シェーダー待ち、安全なノード削除） |
| Assets | `/Game/Art/Rendering/Retro` | `M_RetroPost`、`MPC_Retro`、`MF_RetroVertexSnap(_LP)`、`MF_RetroAffine2/3`、`MF_RetroTexel`、`Materials/M_RetroFBXPhong`、`Baked/*_Retro` |
| Scripts | `Scripts/*.py` | `SetupRetroAssets.py`（共有アセット生成）、`PatchRetroMaterials.py`（親マテリアルへの組み込み・復元）、`CheckRetroMaterials.py`（整合性チェック） |

コンソール：`Retro.Preset PS1|N64|PS2|CRT|OFF`、`Retro.CRT 1`、`Retro.Set VertexSnapStrength 0.5`

## 各機能の実装方式

- **Pixelation**：仮想グリッドの中心を1回だけサンプリングするUV量子化（最近傍）。`PixelResolutionX=0`のときは正方形ピクセル（240行なら16:9で427列、4:3で320列）。`IntegerScale`は1ブロックを整数ピクセルにそろえる設定で、1080pでは270行になる。`bLowResolutionRender`は実際に低解像度で描画する（`r.ScreenPercentage`＋最近傍アップスケール、ゲームワールドのみ）。
- **色数削減とディザ**：Bayer 4×4／8×8。仮想ピクセルに固定しているため時間方向のちらつきはない。
- **頂点スナップ**：WPOで、クリップ空間XYを`SnapResolution`のグリッドに丸める。シャドウパスでは元の位置を使う。NaN・near平面のガードあり。バウンズ半径が`SnapMaxObjectRadius`（既定15m）を超える物体は、効果をフェードで弱める。2三角形の巨大な地面が傾いて下の床が見えるのを防ぐため。
- **アフィン**：Value×wとwを補間し、ピクセル側で割る。三角形ごとに画面空間で線形となり、近似ではなく真のアフィン写像になる。頂点で評価できる入力（TexCoord／WorldPosition）にのみ使える。
- **テクセルスナップ**：UVを仮想テクセル格子に丸める（`TextureResolution`texel/UV）。ミップは丸める前のUVの勾配（ビューのミップバイアス込み）を使う。テクスチャアセットは変更しない。
- **N64**：仮想ピクセル間の3点フィルタ、深度フォグ、ミップバイアス。**CRT**：仮想行に合わせた走査線、蛍光体マスク、湾曲、クロマのにじみ、色収差、ビネット。どのプリセットにも重ねられる。

## プリセット

解像度・色数・有無は公開表に準拠しています。強度や色調、テクスチャ値は推定値で、Stage1で調整しました。

| | PS1 | N64 | PS2 | CRT |
|---|---|---|---|---|
| 仮想行 | 240（正方形ピクセル、整数倍） | 240 | 448 | 200 |
| RGB段階 | 32 | 32 | 256 | 256 |
| ディザ | 0.85（4×4） | 0.3 | 0.5 | 1.0（256段階のため目立たない） |
| 頂点スナップ | 1.0（160×120） | Off | Off | Off |
| アフィン／テクセル | 1.0／1.0（64） | Off／ミップ+1.5 | Off | Off |
| その他 | TAA Off、反射・AO Off、ポストFX Off、雲 Off、彩度1.2 | 3点フィルタ、フォグ0.55 | ソフト0.25 | 走査線0.35、マスク0.08、湾曲0.03、にじみ1 |

## 既存アセットへの変更と復元

| 変更 | 内容 | 戻し方 |
|---|---|---|
| 親マテリアル11個（M_FlatCol、M_LowPoly、M_LowPolyGrass、M_LowPolyWater、Stage01 Ground／Grass、Cage×4、M_RetroFBXPhong） | `RetroRender`タグ付きノードを追加。MPCの既定値0では無効 | `PatchRetroMaterials.py -RetroUnpatch`。Stage01とCageはgit。InportAssetsは`Saved/RetroRender/MaterialBackup` |
| FBX MI 4個（Player、Boar、網、檻） | 親を`M_RetroFBXPhong`に差し替え（パラメータ名は同一） | `-RetroUnpatch`で元のInterchange親に戻る（`PatchRetroMaterials.json`に記録） |
| `MPC_UnlitStrength` | スナップ用スカラー4つを追加（既定値0） | 削除しても無害（参照は`_LP`関数のみ） |
| テクスチャ | パネルで明示的に適用した場合のみ。元の値はパッケージのメタデータに保存 | Textures › Revert |
| 配置メッシュ | パネルで明示的に置換した場合のみ。元パスをComponentTagに保存（Undo可） | Meshes › Restore |

**重要**：`SetupRetroAssets.py`は関数のInput/Outputノードを再利用します。作り直すと、呼び出し側マテリアルの接続がGUID不一致で外れるためです。変更後は必ず`CheckRetroMaterials.py`で確認してください。

## 制約

- UEのマテリアルが参照できるパラメータコレクションは2つまでです。LowPolyパックでは`_LP`版を使います。`M_LowPolyTerrain`（Material Attributes構成）は未対応です。
- 水は頂点シェーダーでテクスチャを読むため、アフィンとテクセルは適用せず、頂点スナップのみです。
- エンジンのコンテンツ（SkyDome、M_ProcGrid）は変更しません。
- Pixelation（PP方式）は描画コストを下げません。下げたい場合は`bLowResolutionRender`を使います。
- Landscape→StaticMesh変換は未実装です（Stage1にLandscapeはありません）。SkeletalMeshの減ポリは対象外です。
