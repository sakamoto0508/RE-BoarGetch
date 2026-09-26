# 発光枠・Stage Carousel — 2026-09-24

他画面への発光展開と最新Notionとの照合結果は [UI_Notion_Audit_2026-09-24.md](UI_Notion_Audit_2026-09-24.md) を参照。

## 参照と適用範囲

- [世界観・UI共通仕様](https://app.notion.com/p/3dcb6c887525810faf13d77dc129f8f4)（取得時の更新日時：2026-09-23 16:19 UTC）
- [ステージ選択仕様](https://app.notion.com/p/3dfb6c88752581bf8e85e4c65fca9b37)（2026-09-23 15:01 UTC）
- UE 5.8.2 / CL 56702186。提示されたUnityコードは表現の参考として使用し、UE UI Materialとして別実装。
- Notionの選択色はアンバーだが、今回のユーザー指定に従い操作枠のFocus／Hoverは緑。CLEARはアンバーを維持。
- 3D世界、StageConfigの内容、StageEntranceの遷移／保存処理は変更していない。

## 発光枠の設定

Content Browserの `/Game/UI/Prototype/MI_UI_FlowGlow_Button` を開き、必要なパラメータをOverrideする。

| Parameter | 初期値 | 用途 |
|---|---:|---|
| Tint | 水色 | 通常枠の色 |
| ActiveTint | 緑 | Focus／Hover時の色 |
| Intensity | 0.6 | 通常時の強度 |
| ActiveIntensity | 1.6 | 選択時の強度 |
| Opacity | 0.9 | 枠＋にじみの透過量 |
| FlowSpeed | 0.22 | 周回速度。0で停止、負値で逆方向 |
| FlowWidth | 0.12 | 光帯の幅（周回位相に対する割合） |
| FlowStrength | 0.75 | 光帯による強調量 |
| Thickness | 1.4 | 基本線幅。選択時は1.8倍 |
| GlowRadius | 3.2 | にじみの幅 |
| Inset | 12 | Image外縁から枠中心までの余白 |
| CornerCut | 8 | 角の切り欠き |
| Width / Height | 自動 | Widgetのローカル寸法 |

親は `/Game/UI/Prototype/M_UI_FlowGlow`。Material Domain=User Interface、Blend Mode=Translucent。Custom式のRGBをFinal Color、AをOpacityへ接続。中央は透明で、枠付近だけに解析的なぼかしを描く。Bloomや新規Textureを必要としない。

周回はMaterialのTimeで駆動し、Pause中も動く。単一Imageの連続UVを使用し、9スライスを使用しないため境界で位相が途切れない。幅・高さは描画寸法に合わせ、DPIを二重に掛けない。

`WBP_HoloActionFrame` と `WBP_HoloMenuEntry` の既存FocusFrame Imageへ適用。親Class、NamedSlot、Button、OnInvoked、FocusEntryは維持。FocusPath追加／除去とMouseEnter／Leaveから `SetFlowFrameState` を呼び、個別のDynamic Materialへ反映する。FocusとHoverを別々に保持し、片方が外れても他方が残れば強調を保つ。拡大率は既存の1.035倍。

Tickは `ResizeFlowFrame` で寸法変化のみを確認し、値が変わったときだけWidth／Heightを書き込む。発光アニメーションや入力状態の毎フレームBindingには使用しない。

新しいボタンには共通Widgetを使用する。発光ImageはHitTestInvisible、ClippingはInherit。発光をImage内部の余白に収めている。GlowRadiusを増やす場合はInsetも増やし、親ScrollBoxの端にはFocus拡大分の余白を確保する。BrushはImageのまま使用し、Box/9-sliceには変更しない。

既存 `M_UI_HoloHUD_Prototype` は濃紺の面・Grid・Scanlineを減光。追加したBorderOpacityは既定1、共通ボタンのNormal MIだけ0にして旧固定枠と新しい動的枠が二重に出ないようにした。文字の色・輝度は発光Materialで変更しない。

## Stage Carousel

既存 `WBP_LobbyStageSelect` 内にPrevious／Current／Nextの3枠を追加。旧CatalogPanelは削除せずCollapsed。StagePanelは1600×940、中央Previewは600×350、左右は0.72倍。Header、下部Mission Data、右下の既存出発／戻る／図鑑ボタンへ再配置。

- `bUseCarousel=true`。従来の一覧処理はSourceに保持。
- StageCatalogの順序を使用し、端は循環しない。存在しない側の枠を非表示。
- 前回Stageの初期選択、保存データ由来のCLEAR／コイン／図鑑進捗を維持。
- 未解放Stageも中央へ移せるが、名前は???、Mission Dataは非開示、出発不可。Lock図形とLOCKEDを表示。
- 左右／D-pad左右／LB・RBで横スライド。`CarouselDuration`の仮調整値は0.18秒（Class Defaultsで変更可）。連打は直前の切替を確定して次の要求を受理し、切替中は出発不可。
- Header／Mission Data／Badgeは中央確定時に更新。CLEARはPreview上へ重ねる。
- 3枠のサムネイル表示を使用するため、旧StageThumbnailImageWidgetNameはNone。旧Image自体は削除していない。
- 現在のStageCatalogはDA_TestStageConfigの1件のみ。新しいStageや個体データは作成していない。

## 2026-09-26 Diorama / Visual更新

`WBP_LobbyStageSelect`を1800×1000の中央配置へ調整。半透明の濃紺Grid背景、上部Headerと矢印、700×525の中央Preview、下部Mission Data、右下Confirm／Back。CatalogPanelと旧ThumbnailはCollapsedを維持。図鑑ボタンは削除せずCollapsed（図鑑機能自体は維持）。数値更新は既存StageProgress／StageConfig／SaveDataを使用する。

`/Game/UI/StagePreview/BP_Stage01_Diorama`は53個の表示専用Primitiveからなる小型模型。草地・土の周回路・Cage・水・風車・研究施設を表現。`ABoarStagePreviewActor`のSceneCapture2Dが表示時だけ1024×768の透過RenderTargetへ撮影する。実Levelはロードせず、Gameplay Actorを使用しない。CollisionとNav影響なし、通常のゲームカメラには描画しない。画面終了時にActor／RT参照を解放し、最大3Stageのみ保持する。

StageConfigに任意の`PreviewActorClass`を追加。既存DA_TestStageConfigの表示用参照だけ設定し、StageCatalog、StageEntrance、Save、遷移条件は変更しない。Preview未登録・生成失敗時は従来Thumbnailへfallback。Stage 1以外の架空データは未追加。

Currentは通常色、Previousは低彩度、Next／LOCKEDはさらに低彩度で輪郭と青みを残す。既存スライド時間・470px移動・端点処理・LastAttemptedStage・入力・CLEAR／LOCKED処理は維持。

C++ Development Editorビルド成功。Widget／Preview Blueprint Compile・Save成功。保存後のPreview参照、53パーツ、bUseCarousel、CatalogPanel Collapsed、対象Assetの未保存なしを確認。PIE／Runtime未実施。実機での透過描画、文字配置、Gamepad、連続開閉、複数Stage切替、LOCKED表示はユーザー確認待ち。

## 前回時点の未実装・素材待ち（上記更新前）

既存Previewアセットは検索で見つからず、3Dジオラマ／SceneCapture／RenderTargetは今回追加していない。サムネイルがあれば暫定表示し、なければ「素材未登録」と表示する。Next／未解放は暫定的に減光したサムネイル表現で、最終仕様の3Dシルエット表現は未完成。実Stageの特徴を簡略化したジオラマ素材と、そのPreview Actor／撮影構成の制作が必要。

## 確認結果

- Development Editor C++ビルド成功。
- 新Materialと変更した共通MaterialをCompile・Save。保存時のShaderログも確認。初回のHLSL予約語エラーは修正済み。
- 共通2 Widget、Stage Select、Title、Result、PauseMenu、Settings、GameOver、LobbyHUD、Loadout、Encyclopedia、ExitConfirmation関連を警告もエラー扱いでCompile成功。
- 対象10 Assetを保存し、再取得で未保存フラグfalse、親Class、3枠配置、Carousel設定、入力参照、UI Material Domain／透過を確認。
- Designer上の配置のみ確認。DesignerはCollapsed Widgetも編集用に表示するため、旧一覧・未設定Imageが見える場合がある。
- ユーザー指示によりPIE／ゲーム実行テストは未実施。入力連打、実ゲームパッド、実際のDPI変更、FocusとHoverの競合、複数Stage、ロック状態の実行時確認は未確認。
