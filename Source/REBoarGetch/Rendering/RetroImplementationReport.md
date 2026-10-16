# レトロ描画プリセット比較：実装・検証報告

2026年10月16日 / UE 5.8.3（CL 58210709）/ Windows / C++

OFF・PS1・N64・PS2・CRTを同じシーンで切り替える比較システムを実装しました。
有料描画アセットへの依存はありません。起動方法と構成は [README](README.md) を参照してください。

## Phase 1：共通Materialと基本プリセット

**変更内容**

- enum、13項目のパラメータ構造体、初期値用DataAssetを追加。
- 共通Post Process MaterialをMaterialEditingLibraryで実際に生成。
- PS1のPixelation・Color Quantization・固定Bayer 4×4ディザリングを実装。
- N64は仮想低解像度グリッド上の補間、PS2は縦480と控えめな処理に設定。
- ComponentがMIDを1個保持し、切替はScalar更新だけで実行。
- OFFは専用PostProcessComponentを無効化。既存の描画設定へ戻る構成。

**確認結果**

UE5.8.3のC++ビルドと実GPUでのMaterialコンパイルに成功。
`BL_SceneColorAfterTonemapping` の配置を実アセットで確認しました。
PIE内の全モード切替、250回の切替後も同じMIDであること、実際のMIDへの値反映、Resetを確認しました。
GPUプロファイルでも、OFFのフレームには独自Materialのパスがありません。

**問題点・制約**

N64のDistance Fogは未実装。既存の霧・背景色と視認距離に合わせた追加候補として検討しました。
テクスチャの低解像度化、Meshの低ポリゴン化、頂点揺れ、Affine Texture MappingはPost Processの対象外です。

## Phase 2：CRT・UMG・調整

**変更内容**

- CRTの走査線、湾曲、Vignette、色収差、弱いPhosphor Maskを共通Materialに追加。
- 上部の5モードボタン、値付き13スライダー、Reset、Screenshot、Compare 5をネイティブUMGで作成。
- 実MappingContext内にF7が使われていないことを調査し、Ctrl+Alt+F7を採用。
- UIOnlyで開き、閉じるとGameOnly・カーソル・自分が取得した移動/視点ロックを復元。
- 既存メニュー・ポーズ中からの起動を抑止。Shippingでは開発UIと撮影の入口を無効化。
- 現在プリセット名は画面左下へ表示。デバッグパネルはHUDより手前、画面高さに追従するScrollBox付き。

**確認結果**

C++ビルド成功。実PIEでUMG生成、13行の表示、5ボタンのコールバック、
スライダーのコールバックからMIDへの即時反映、同じMIDの保持、閉じた後の入力ロック解除を確認しました。
実際の画面を撮影し、初回に見つかったHUDとの重なり・小さいPIEで下部が切れる問題を修正しました。

**問題点・未検証**

キー・マウスを物理操作するテストは行っていません。実キー押下、スライダーのマウスドラッグ、
長時間のゲーム操作、すべての施設メニューとの組み合わせは未検証です。
UIの切替・値変更は実UMGのコールバックをPythonから呼び出して検証しています。

## Phase 3：画像比較・負荷・ビジュアル確認

**変更内容**

- ゲーム時間とカメラを固定し、OFFから5モードを自動撮影する処理を追加。
- 各モード30描画フレーム待ち、日時・識別子・モード名付きPNGを保存。
- 完了時・タイムアウト時は元のカメラ・モード・ポーズ状態へ復帰。
- ライブPIEの描画Viewportを変更できる開発用APIと自動検証を追加。
- `profilegpu` の単独パス時間、GPU全フレーム時間、比較一覧画像を出力。

**確認結果**

基本自動検証36項目が成功し、SaveGamesのファイル内容も変わりませんでした。
同じPIE内で1280×720、1024×768、1600×900へ描画Viewportを変更し、各5枚・計15枚を生成。
PNGの実寸も確認済みです。4:3と16:9の画像を開き、UVの位置ずれや画面端への別領域の混入がないことを確認しました。

画像では、PS1の粗い輪郭とディザー、N64の柔らかい補間、PS2の滑らかさ、CRTの走査線と湾曲を確認できました。
明るい地面に対する主人公の赤い服、ガジェットの形、イノシシの輪郭は確認範囲で判別できます。
HUDを含む画面も別途保存し、HP・捕獲数・ガジェットアイコンが表示されることを確認しました。
動作中の全捕獲演出、特にNiagaraの細い粒子や発光の視認性は未検証です。

比較一覧: [RetroComparison.png](../Verification/RetroComparison.png)

元画像は `C:/UnrealGames/REBoarGetch/Saved/Screenshots/Retro/` にあります。
1280×720の比較は `20261016_153111_CE5E04C7_*.png`、4:3は `20261016_153149_CE0DAD5A_*.png`、
1600×900は `20261016_153154_A23C362F_*.png` です。

### GPU計測

NVIDIA GeForce RTX 4060 Laptop GPU / D3D12 / 1280×720 / Stage1複製 / 静止カメラ。
独自パスは有効時に1フルスクリーン描画、1 draw / 1 primitiveでした。

| モード | 独自Materialパス | GPU全フレーム中央値（120サンプル） |
|---|---:|---:|
| OFF | 0（パスなし） | 7.036 ms |
| PS1 | 0.030 ms | 7.147 ms |
| N64 | 0.037 ms | 7.099 ms |
| PS2 | 0.037 ms | 7.153 ms |
| CRT | 0.027 ms | 7.161 ms |

Materialパス時間は各モード1回の `profilegpu` のinclusive値です。
GPU全フレーム時間は90フレームの待機後に取得し、プロファイル実行前の120サンプルを使用しました。
全フレーム差にはEditor描画や他パスの変動が含まれるため、独自処理コストとして差分を断定しません。
1080p以上・ほかのGPU・製品ビルドでの負荷は未検証です。

共通パス化、OFFのパス除去、MID再利用、入力テクスチャ範囲に基づくUV変換で初期最適化しました。
Pixelationは元シーンを低解像度でレンダリングする機能ではなく、Lumenなどの描画負荷はそのままです。

### TAA / TSR

プロジェクトの初期AAはTSR（`r.AntiAliasingMethod=4`）でした。
TAA（2）とTSR（4）でPS1モードを実際に描画し、静止時の画像を各2枚保存しました。
固定Bayerは画面上に保持され、時間乱数によるディザー移動はありません。
入力のTAA/TSR由来の柔らかさは残ります。

静止画2枚間の差分集計は `Verification/RetroVisualChecks/TemporalDifference.json` にあります。
これだけでは高速移動時の輪郭のちらつきや残像の評価にはなりません。
走行・カメラ旋回・捕獲中の時間方向の主観評価は未検証です。AA設定は検証終了時に復元します。

### 参考動画に対する判断

動画5秒地点の明るい緑、赤い主人公、青いガジェット、粗いテクスチャのまとまりを確認しました。
今回のStageではPS1がもっともレトロ感を明確に出し、N64は柔らかく親しみやすい印象です。
初代サルゲッチュへ寄せる次の候補は、PS1のDitheringStrengthを0.3〜0.55程度で比較しながら、
テクスチャ解像度と大きな色面を調整することです。これは画像からの改善案であり、今回の既存アート変更には含めていません。

## 導入範囲・調査

- 新規アセット: `/Game/Art/Rendering/Retro/M_RetroCompare`、`DA_RetroPresets`、`L_RetroCompare`。
- 比較Level内に既存PostProcessVolumeや配置CameraActorはなく、CameraはプレイヤーのFollowCameraを使用。
- CapturePresentationComponentは既存のFOV・CameraShake・Niagaraを担当しており、描画Componentと責務を分離。
- C++既存ファイルの変更はBuild.csへのPrivate依存 `RHI` 追加のみ。
- 既存GameMode・PlayerController・Character・Stage1アセットに今回の描画処理を追加していません。
- 生成スクリプトは既存アセットを標準では保存・上書きしません。比較LevelはStage1から新規作成。

起動時に既存のGameFeatureDataのAssetManager設定エラーが出ます。
生成コマンドレットはそのエラーのため終了コード1でしたが、Pythonスクリプト自体は成功し、3アセットの保存を確認しています。
その後の実GPU・実PIE検証は完了しています。この既存設定の修正は今回の変更範囲に含めていません。

## 検証項目との対応

| 項目 | 結果 |
|---|---|
| 1. PIE中の全モード切替 | 確認済み（UMGコールバック経由を含む） |
| 2. OFFで元の描画 | Component無効化・GPUパス不在を確認 |
| 3. Slider即時反映 | コールバック→MID反映確認。物理ドラッグは未検証 |
| 4. 解像度・アスペクト比 | ライブPIEの描画Viewport3サイズと15画像を確認。OSウィンドウの手動ドラッグは未検証 |
| 5. HUD・ガジェット | HUDと装備中の網を画像確認。全捕獲VFXは未検証 |
| 6. イノシシ視認性 | 比較Stageの静止画で確認。全Stage・距離・背景の網羅評価は未検証 |
| 7. 不要な繰り返し生成 | 250回切替後の同一MID保持と独自1パスを確認 |
| 8. GPU負荷 | RTX4060 Laptop・D3D12・720pでパス時間と全フレーム時間を実測 |

再現用スクリプト: `RetroPieSmoke.py`、`RetroVisualChecks.py`。
機械可読結果: `RetroPieResults.json`、`RetroEvidenceSummary.json`、`RetroVisualChecks/Results.json`。
いずれも `Source/REBoarGetch/Verification/` 配下です。

HDR出力、分割画面別プリセット、Shippingパッケージの実行は未検証です。
