# RE-BoarGetch レトロ描画比較

## 起動・操作

UE5.8で `/Game/Art/Rendering/Retro/L_RetroCompare` を開き、PIEを開始します。
Stage1を複製した専用Levelです。既存StageのGameMode・Controller・HUDを使います。

- **Ctrl+Alt+F7**: 開発用UMGの開閉。ゲーム操作中から開きます。既存メニュー・ポーズ中は開きません。
- **OFF / PS1 / N64 / PS2 / CRT**: 即時切替。OFFは独自Post Processを無効化します。
- **Slider**: 現在のプリセットだけを変更。切替後も同じPIE内では調整値を保持します。
- **Reset**: 現在のプリセットをDataAssetの初期値に戻します。
- **Screenshot**: 現在のモードを保存。
- **Compare 5**: 時間とカメラを固定し、OFFから順に5枚保存。終了後は元のカメラ・プリセット・ポーズ状態へ戻します。

画像の保存先は `Saved/Screenshots/Retro/日時_識別子_プリセット名.png`。
比較中はUIを閉じ、各切替後30描画フレーム待ちます。ゲームのUMGを含めないシーン比較画像です。
既存HUDや捕獲VFXの目視確認は通常のPIE画面で行ってください。
調整値はランタイムのみ。初期値を保存したい場合は `DA_RetroPresets` を編集します。

## 構成・設計理由

| 要素 | 責務 |
|---|---|
| `ERetroRenderPreset` | 5モードの識別 |
| `FRetroRenderParameters` | 独立した13項目の数値 |
| `URetroRenderPresetData` | モード別初期値。配列はOFF/PS1/N64/PS2/CRTの順序 |
| `URetroRenderComponent` | MIDを1個保持し、パラメータ更新とPost Processの有効化を担当 |
| `M_RetroCompare` / `RetroCompare.hlsl` | 共通フルスクリーンパスの描画 |
| `ARetroCompareActor` | テストLevelへの導入、デバッグ入力、同一カメラでの撮影、計測API |
| `URetroDebugWidget` / `URetroParameterRow` | ネイティブUMGの表示と操作 |
| `URetroStatusWidget` | 通常HUDと重ならない画面左下の現在モード表示 |

既存の `ABoarPlayerController` はEnhanced Input・ゲーム進行・UIFlowを扱い、
`UCapturePresentationComponent` はFOV・CameraShake・Niagaraを扱っています。
これらに描画責務を追加せず、専用ActorにComponentを持たせました。
新しいSubsystemやGameModeの継承は不要です。

描画Componentは専用のunbound `UPostProcessComponent` を1つ登録します。
既存VolumeやCameraの設定を書き換えず、WeightedBlendableを1つだけ追加します。
MID生成はBeginPlayで1回。モード変更・スライダー操作・Resetで生成し直しません。
EndPlayで登録解除します。複数プレイヤーの別々の画面へ別プリセットを適用する用途は対象外です。

UMGは開いている間だけUIOnlyとし、移動・視点ロックを1回取得して閉じると1回解放します。
既存メニューの上へ開かず、IgnoreInputの全リセットも行いません。
Shippingではデバッグ入力・UI起動・撮影をコンパイル時に無効化します。
製品向けLevelには比較Actorを置かず、比較LevelをCook対象から外せます。
製品でプリセットを使う場合は描画ComponentのBlueprint APIだけ利用できます。

## 描画方針

| モード | 初期値の狙い |
|---|---|
| OFF | 独自パスなし。プロジェクト本来の画質 |
| PS1 | 縦240、Pixelation 1、32階調/チャンネル、固定Bayer 4x4、Saturation 1.15 |
| N64 | 縦240、仮想画素グリッド上の4タップ補間、約91階調、Saturation 1.08 |
| PS2 | 縦480、Pixelation 0.35、弱い色数制限・補間 |
| CRT | Scanline 0.16、湾曲0.025、色ずれ0.65画素、Vignette 0.12、Mask 0.035 |

ColorQuantizationは0でバイパス、1で4階調、0.5で32階調です。
DitheringStrengthは量子化の1段分を基準にした強さです。
すべてのパラメータを独立したMaterial Scalarとして公開しています。
N64のDistance Fogは、既存の霧・背景とイノシシの視認性への影響を避けるため未実装です。
別途、Stageに合わせた距離・色を決めてから追加する候補としました。

UE5.8の `BL_SceneColorAfterTonemapping` を使用。
縦解像度×実際の入力アスペクト比から横画素数を求めます。
ViewportUVで画素化・湾曲し、`ViewportUVToSceneTextureUV` と `ClampSceneTextureUV`
でPostProcessInput0の実際のテクスチャ領域へ変換します。
共有レンダーターゲットの余白や、PIEのオフセットを直接UVとして使いません。
TAA/TSRより後で固定Bayerを描くため、ディザーを時間方向に蓄積しません。
ただし、入力画像にあるTAA/TSRのぼけや動く輪郭のちらつきを除去する機能ではありません。
SDRでの比較を対象とし、HDR出力の色空間は未検証です。

追加フルスクリーンパスは有効時1つ、OFFは0です。
基本色取得1タップに、SoftSampling有効時4タップ、色ずれ有効時2タップを追加します。
GPUの分岐最適化次第で実際の実行コストは変わります。
画素化は見た目だけで、元シーンの描画解像度やLumenのコストを減らしません。

## アセット生成・再現

生成済みアセットはすべて `/Game/Art/Rendering/Retro/` にあります。
ソースのない環境から作成する場合はC++をビルドしてから、EditorのPythonコンソールで実行します。

```python
import runpy
runpy.run_path(r'C:/UnrealGames/REBoarGetch/Source/REBoarGetch/Rendering/SetupRetroAssets.py')
```

既存アセットは標準では上書きしません。HLSLから生成済みMaterialを作り直す場合のみ、
Editorを `-RetroRebuildMaterial` 付きで起動して実行します。この指定はMaterialのグラフを置き換えます。
DataAssetの値や既存Levelは上書きしません。

## 検証

自動検証: `Verification/RetroPieSmoke.py`。
UEの `-ExecutePythonScript` で実GPUのEditorを起動し、結果を `Verification/RetroPieResults.json` へ保存します。
最終的な実行結果・GPU計測値・未検証事項は `RetroImplementationReport.md` に記載します。

## 動画の雰囲気へ近づける次の改善

添付動画5秒地点では、明るい緑の地面、赤い主人公、青いガジェット、単純な地形の輪郭が読みやすく、
粗いテクスチャにも大きな色のまとまりがあります。初期PS1値はこの方向を優先しています。

1. テクスチャを低解像度で用意し、必要な物だけNearest寄りのFilterを設定する。
2. イノシシとガジェットの形が背景に埋もれないよう、大きな色面と明度差を作る。
3. テストStageの照明・露出・影を整理し、暗い部分のつぶれを減らす。
4. メッシュ自体のシルエットを単純化する。頂点揺れ・Affine Texture Mappingは別のMesh/Material機能として検討する。

これらはPost Processだけでは再現できず、今回の実装では既存Meshやテクスチャを変更していません。
