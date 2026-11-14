# RetroRender（RE-BoarGetch）

PS1 / N64 / PS2 / CRT の見た目を、エディタとPIEで切り替えるプロジェクトプラグインです。
Fabの「Retro Render」の公開仕様を参考にした独自実装で、製品のコードやアセットは使っていません。

## 構成

| 層 | 実体 | 役割 |
|---|---|---|
| Runtime | `URetroRenderComponent` | **1ワールドにつき1つ**（2つ目は警告ログを出して無効）。CVarはプロセス共通のリクエストスタックで管理し、最後の要求者が解放した時だけ元値へ戻す。1つのunbound PostProcessでMIDを適用。`MPC_Retro`と、プロジェクト設定の Additional Parameter Collections（本プロジェクトでは`MPC_UnlitStrength`）に形状系の値を書き込む。AA・Lumen反射・ポストFX・雲・低解像度描画の切り替えを、OFFや登録解除時に復元 |
| Runtime | `ARetroRenderActor` | レベルに置く設定アクタ（エディタのビューポートでもプレビュー可） |
| Runtime | `ARetroCompareActor` | 比較・撮影用。Ctrl+Alt+F7のUMG、`+CRT`ボタン、自動撮影API |
| Runtime | `FRetroRenderParameters` / `DA_RetroPresets` | プリセットの値。`GetRetroParameterDescriptors()`でマテリアル名・MPC名・UI範囲を一元管理 |
| Editor | Window/Tools ›「Retro Render」タブ | Look（プリセット・全パラメータの即時反映・CRT重ね・レベル追加・既定値保存・OFF）、Textures、Meshes |
| Editor | `URetroRenderEditorLibrary` | Python／Blueprintから同じ処理を呼ぶ関数群（テクスチャ、Bake、シェーダー待ち、安全なノード削除） |
| Assets（プラグイン同梱） | `/RetroRender`（`Plugins/RetroRender/Content`） | `M_RetroPost`、`MPC_Retro`、`DA_RetroPresets`、`MF_RetroVertexSnap(_LP)`、`MF_RetroAffine2/3`、`MF_RetroTexel`、`Materials/M_RetroFBXPhong` |
| Assets（本プロジェクト専用） | `/Game/Art/Rendering/Retro` | `Baked/*_Retro`、比較用の`L_RetroCompare`／`M_RetroCompare` |
| Scripts | `Scripts/*.py` | `SetupRetroAssets.py`（共有アセット生成）、`PatchRetroMaterials.py`（親マテリアルへの組み込み・復元）、`CheckRetroMaterials.py`（整合性チェック）、`retro_project.py`（共通：プロジェクト設定の読み込み） |
| プロジェクト設定 | `<Project>/Config/RetroRender/` | `RetroRenderProject.json`（パッチ対象・追加MPC・検証用の情報）、`PatchRecord.json`（パッチ記録）。プラグインにはプロジェクト固有の値を書かない |

コンソール：`Retro.Preset PS1|N64|PS2|CRT|OFF`、`Retro.CRT 1`、`Retro.Set VertexSnapStrength 0.5`

## プロジェクト全体への適用（常時オン）

**Project Settings › Plugins › Retro Render**（`Config/DefaultGame.ini` の `[/Script/RetroRender.RetroRenderSettings]`）

| 項目 | 既定値 | 内容 |
|---|---|---|
| Auto Apply In Game | ON | すべてのゲームワールド（PIE／パッケージ版）の開始時に、`URetroRenderWorldSubsystem` が一時アクタ `RetroRender_ProjectDefault` を生成する（レベルには保存されない） |
| Default Preset | PS1 | 自動適用するプリセット |
| Default CRT Overlay | ON | CRTレイヤーを重ねる |
| Excluded Maps | なし | 自動適用しないマップ |
| Additional Parameter Collections | なし（本プロジェクトは`MPC_UnlitStrength`） | `MPC_Retro`に加えて形状系スカラーを書き込むMPC。MPCを既に2つ使っているマテリアル用。存在しないものは無視 |

レベルに `Retro Render Settings` または `RetroCompareActor` が置いてある場合は、そちらが優先される（1ワールドにつき1つ）。プレイ中の切り替えは、従来どおりコンソールの `Retro.Preset` や `Retro.CRT` で行う。OFFにすると、自動適用中でも元の描画に戻る。

頂点スナップなどの形状系の効果は、パッチを当てた親マテリアルだけに効く。対象はL_Stage01、L_Lobby、L_GadgetLab、L_Archiveで使われているプロジェクトマテリアル（`Config/RetroRender/RetroRenderProject.json` の `targets`、24個＋プラグイン内の`M_RetroFBXPhong`）。エンジン／Interchange由来のマテリアル（BasicShape、glTF）、空（`M_Facility_Sky`）、`M_LowPolyTerrain` はポストプロセスのみが効く。

## 新しいレベル・マテリアルへの対応（ツール）

- **レベルごとの作業は不要**。ポストプロセス（画素化・色数・ディザ・CRT）はプロジェクト設定によって全ゲームワールドに自動で効く。
- 頂点スナップなど形状系の効果は、**新しい親マテリアル**に一度だけパッチを当てれば、そのマテリアルを使うすべてのレベルで有効になる。
  1. レベルを開くと、未パッチのマテリアルがあれば通知が出る（Project Settings › Retro Render › Notify Unpatched Materials でOFFにできる）
  2. Tools › Retro Render › Look の **Scan Level Materials** で一覧を確認する（何も変更しない）
  3. **Patch Level Materials** を押すと、確認ダイアログのあと `[patch]`／`[reparent]` の対象だけを処理する（事前にバックアップを取り、失敗した場合はメモリ上で元に戻すので、壊れた状態でdirtyにならない）
- 自動でスキップされるもの：エンジン／プラグイン由来のマテリアル、Sky、Material Attributesのグラフ、すでにパラメータコレクションを2つ使っているもの。理由は一覧に表示される。
- Scan／Patch には **Python Editor Script Plugin** が必要（本プラグインの必須依存ではない。無効なときはその旨を表示するだけ）。
- 見つけたマテリアルは `Config/RetroRender/PatchRecord.json` の `auto_targets` に記録され、`-RetroUnpatch` と `CheckRetroMaterials.py` の対象にも含まれる。

## 各機能の実装方式

- **Pixelation**：仮想グリッドの中心を1回だけサンプリングするUV量子化（最近傍）。`PixelResolutionX=0`のときは正方形ピクセル（240行なら16:9で427列、4:3で320列）。`IntegerScale`は1ブロックを整数ピクセルにそろえる設定で、1080pでは270行になる。`bLowResolutionRender`は実際に低解像度で描画する（`r.ScreenPercentage`＋最近傍アップスケール、ゲームワールドのみ）。
- **色数削減とディザ**：Bayer 4×4／8×8。仮想ピクセルに固定しているため時間方向のちらつきはない。
- **頂点スナップ**：WPOで、クリップ空間XYを`SnapResolution`のグリッドに丸める。シャドウパスでは元の位置を使う。NaN・near平面のガードあり。バウンズ半径が`SnapMaxObjectRadius`（既定15m）を超える物体は、効果をフェードで弱める。2三角形の巨大な地面が傾いて下の床が見えるのを防ぐため。
- **平らな物体は頂点スナップしない**：バウンズの高さ÷幅が `SnapFlatRatio`（既定 0.08、Look タブの Geometry で調整可、0 で無効）未満の物体（広場、道、水面など）は、スナップを弱める（0.06〜0.08 でフェード）。細かく分割された円形の床をスナップすると、縁がギザギザになり、地面との継ぎ目が開くため（例：`SM_S01R_CagePlaza`）。
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
| 親マテリアル24個＋`M_RetroFBXPhong`（`RetroRenderProject.json` の `targets`：InportAssets 11個〔FlatCol、LowPoly×3、Farm_House、Clinic×6〕、Lobby 7個、Stage01 Ground／Grass、Art/Materials/Cage×4） | `RetroRender`タグ付きノードを追加。MPCの既定値0では無効 | `PatchRetroMaterials.py -RetroUnpatch`。Stage01とCageはgit。InportAssetsは`Saved/RetroRender/MaterialBackup` |
| FBX MI 4個（Player、Boar、網、檻） | 親を`M_RetroFBXPhong`に差し替え（パラメータ名は同一） | `-RetroUnpatch`で元のInterchange親に戻る（`Config/RetroRender/PatchRecord.json`に記録） |
| `MPC_UnlitStrength` | スナップ用スカラー4つを追加（既定値0） | 削除しても無害（参照は`_LP`関数のみ） |
| テクスチャ | パネルで明示的に適用した場合のみ。元の値はパッケージのメタデータに保存 | Textures › Revert |
| 配置メッシュ | パネルで明示的に置換した場合のみ。元パスをComponentTagに保存（Undo可） | Meshes › Restore |

**重要**：`SetupRetroAssets.py`は関数のInput/Outputノードを再利用します。作り直すと、呼び出し側マテリアルの接続がGUID不一致で外れるためです。変更後は必ず`CheckRetroMaterials.py`で確認してください。

## InportAssets（git管理外）への依存と別環境での導入

`Content/InportAssets/` は `.gitignore` 対象です。Stage1自体がこのフォルダの資産（LowPolyパック、LevelPrototyping、Player/Boar、網、檻）を使うため、プロジェクトを開くには元々このフォルダが必要です。RetroRenderが変更・参照するパッケージの一覧はプロジェクト側の `Scripts/RetroRender/InportAssetsDependencies.md` にあり、同じフォルダの `InventoryInportAssets.py` で再生成できます（どちらも本プロジェクト専用のためプラグイン外に置いています）。

**別環境での導入**（`InportAssets` は従来どおりチーム内の共有手段で配置しておく）
1. git checkoutしてビルド（`RetroRender` プラグインは自動で有効）
2. 次のどちらかで、InportAssets側の変更を再現する
   - **A. 再生成（推奨）**：エディタで `Scripts/SetupRetroAssets.py` → `Scripts/PatchRetroMaterials.py` を実行（`run_ue_gui.sh` でも可）。パッチは元のWPOチェーンを自動で検出して繋ぐので、未変更のパックからそのまま再現できる。パックがない場合、そのマテリアルは `missing` としてスキップされる。
   - **B. バックアップから復元**：エディタを閉じた状態で `powershell -File Scripts/RetroRender/InportAssetsBackup.ps1 -Restore -Archive <zip>`（プロジェクトルートから） を実行（SHA256で検証）
3. `Scripts/CheckRetroMaterials.py` で、存在する対象マテリアルがすべて `OK` になることを確認

**バックアップ**：`Scripts/RetroRender/InportAssetsBackup.ps1 -Backup -Archive <zip>` で、変更済みのパッケージ（現在16個）とハッシュを1つのzipにまとめる。パッチを当てる前の未変更コピーは、パッチ／セットアップ実行時に `Saved/RetroRender/MaterialBackup` へ自動保存される（初回のみ）。そのコピーは `-Restore -Pristine` で書き戻せる。

**元に戻す**：`PatchRetroMaterials.py -RetroUnpatch` で、マテリアルとMIの親が元に戻る。`MPC_UnlitStrength` に追加したスカラー4つは既定値0の不活性データとして残る。完全に元のファイルに戻すときは `-Restore -Pristine` を使う。

## 別プロジェクトへの導入手順

ポストプロセス（画素化・色数・ディザ・CRT）は、プラグインをコピーして有効化するだけで動きます。

1. `Plugins/RetroRender` をフォルダごと、導入先プロジェクトの `Plugins/` にコピーする（`Binaries`／`Intermediate` は不要）。`.uasset` はプラグイン内の相対位置を保ったままコピーすること（`/RetroRender/...` のパスで参照し合っているため）。
2. C++ プロジェクトならビルドする（Blueprint のみのプロジェクトは、エディタ起動時のビルド確認で「はい」）。
3. Edit › Plugins で **Retro Render** を有効化し、エディタを再起動する。
4. 何もしなくても、すべてのゲームワールド（PIE／パッケージ版）に PS1＋CRT が自動適用される。変更は Project Settings › Plugins › Retro Render（`Config/DefaultGame.ini` に保存）。
5. パッケージ版：`M_RetroPost`／`MPC_Retro`／`DA_RetroPresets`（と Additional Parameter Collections）は、エディタモジュールがクック時に自動で追加する。

**形状系（頂点スナップ・アフィン・テクセルスナップ）を使う場合**（任意）
1. Edit › Plugins で **Python Editor Script Plugin** を有効化する。
2. レベルを開き、Tools › Retro Render › Look の **Scan / Patch Level Materials** でパッチを当てる。対象は `Config/RetroRender/PatchRecord.json` に記録される。
3. まとめて管理したい場合は `Config/RetroRender/RetroRenderProject.json` を作り、`targets`（パスと機能 `snap`／`snap_lp`／`texel`／`world`／`uv`）を書いて `PatchRetroMaterials.py` を実行する。形式は `Scripts/retro_project.py` の先頭コメントを参照。別の場所のファイルを使うときは `-RetroConfig=<path>` を渡す。
4. MPCを既に2つ使っているマテリアルに頂点スナップを入れたい場合は、そのMPCを `snap_collections` と Additional Parameter Collections の両方に登録し、`SetupRetroAssets.py` を実行する（`MF_RetroVertexSnap_LP` がそのMPCを参照するよう作り直される）。

**注意**：同梱の `MF_RetroVertexSnap_LP` は本プロジェクトの `MPC_UnlitStrength` を参照しています。導入先にそのMPCが無いと、この関数を読み込んだときに参照切れの警告が出ます（使わなければ影響なし。必要なら上の4で作り直す）。

## Decimate Bakerの上書き規則
- `_Retro` アセットにはパッケージメタデータ `RetroRender.BakedFrom=<元メッシュ>` を付ける
- 同名アセットが既にある場合、既定では**再Bakeせず既存を使う**（手作業の修正が保たれる）。`Overwrite Existing` をONにしたときだけ再Bakeする
- 同名でもBaker製ではないもの、または別のソースから作られたものは**常にスキップ**する（上書きしない）
- 再Bakeで三角形が減らなかった場合（Naniteのソースなど）は警告を出し、Replaceでは差し替えない

## 制約

- UEのマテリアルが参照できるパラメータコレクションは2つまでです。LowPolyパックでは`_LP`版を使います。`M_LowPolyTerrain`（Material Attributes構成）は未対応です。
- 水は頂点シェーダーでテクスチャを読むため、アフィンとテクセルは適用せず、頂点スナップのみです。
- エンジンのコンテンツ（SkyDome、M_ProcGrid）は変更しません。
- Pixelation（PP方式）は描画コストを下げません。下げたい場合は`bLowResolutionRender`を使います。
- Landscape→StaticMesh変換は未実装です（Stage1にLandscapeはありません）。SkeletalMeshの減ポリは対象外です。
