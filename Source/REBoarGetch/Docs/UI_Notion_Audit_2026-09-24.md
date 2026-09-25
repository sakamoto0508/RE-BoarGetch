# UI発光展開・Notion照合 — 2026-09-24

## 今回の変更

既存 `M_UI_HoloHUD_Prototype` にTimeによる周回光と枠周辺のぼかしを追加した。既存MI、Widget Tree、親Class、入力／データ更新イベントは保持。文字・Grid・Panelの面そのものには周回による明るさ変化を掛けていない。

| 画面・部位 | 適用内容 |
|---|---|
| PlayerHUD | HP、現在収容数、4個のGadget背景・選択枠 |
| LobbyHUD | 目的表示Panel |
| Title | 既存共通MenuEntryの周回発光を継続 |
| Pause／Settings／Lobby復帰確認 | 背景Panelに弱い周回光。共通操作枠は既存実装を継続 |
| Gadget Loadout | Panelと選択済み行の枠 |
| Encyclopedia | 専用MIを追加して装備画面より低輝度に設定。行と戻るは共通枠 |
| Result | 既存Border_BackgroundのMIを通じて適用。通常より強く設定 |
| GameOver | 専用MIを追加してPanelの濃紺を強め、枠の光量を最低に設定。既存の局所赤警告を保持 |
| Stage Select | Mission Dataへ共通Master変更を反映。3枠の既存FlowGlowは継続 |

新規MIは `/Game/UI/Prototype/MI_UI_HoloGameOver_Panel` と `MI_UI_HoloEncyclopedia_Panel`。共用していた元MIは削除せず、各画面のImage Brushの参照だけを変更。

## 調整方法

対象PanelのBrushが参照しているMIを開き、以下をOverrideする。

- `FlowSpeed`：周回速度。0で停止。
- `FlowWidth`：周回光帯の幅。
- `FlowStrength`：光帯の強調量。
- `FlowIntensity`：枠全体の光量。
- `FlowOpacity`：枠の光の透過量。面のFillとは別。
- `GlowRadius`：にじみ幅。現行枠の内側余白は8なので、標準2.4を大きく変更する際は見切れに注意。
- 既存の `Tint`／`Thickness`／`Fill`／`Width`／`Height` は引き続き使用可能。

HUDはFlowIntensity=0.42〜0.5、通常Panel=0.5〜0.75、選択枠／Result=1.1、GameOver=0.35。値は仮調整値であり、仕様上の固定値ではない。GameOverのFill=0.9、図鑑=0.84。

UI Domain／Translucentを維持し、Bloom非依存。TimeはIgnorePause=true。常時表示の光量を抑え、面・文字の読みやすさを保つ。追加Tickはない。固定Panelの寸法は既存MI値を使用するため、将来Panelのレイアウト寸法を変更する際はWidth／Heightも合わせる。共通操作枠は従来どおり実Widget寸法へ自動追従。

## 照合した正本

- [UI共通・アート](https://app.notion.com/p/3dcb6c887525810faf13d77dc129f8f4) — 取得時更新 2026-09-23 16:19 UTC
- [ゲームループ・Pause](https://app.notion.com/p/398b6c887525803b93dedbd9f5231423) — 2026-09-23 14:57 UTC
- [ステージ選択](https://app.notion.com/p/3dfb6c88752581bf8e85e4c65fca9b37) — 2026-09-23 15:01 UTC
- [Result](https://app.notion.com/p/3dfb6c887525810c8ccaf07eb395380c) — 2026-09-19 01:12 UTC
- [図鑑](https://app.notion.com/p/3dfb6c887525814db14af7f661b90c3c) — 2026-09-19 01:26 UTC
- [ガチャメカ](https://app.notion.com/p/398b6c8875258093b8c2ca434b9c3d63) — 2026-09-19 02:45 UTC
- [Player](https://app.notion.com/p/398b6c887525801ab875fda40262b95c) — 2026-09-23 15:05 UTC

以下の「一致」はSource／Blueprint／DataAssetの構造・接続上の判定であり、PIE成功を意味しない。

| 対象 | 一致している部分 | 差分・未完成・未確認 |
|---|---|---|
| 共通Visual | 濃紺・透過・Grid、枠のみ発光、Focus拡大、GameOverの局所赤 | Notionの操作選択は黄〜アンバー、共通操作枠は前回ユーザー指定の緑。HUDの装備選択は従来のアンバー。押下専用の短い発光演出は共通Focus/Hoverとは別途未実装。玩具らしいIcon・造形を含む完成度、実画面での光量は未確認 |
| HUD | HP左上、収容数右上、Gadget4枠右下の構造と既存更新接続を保持 | 実プレイ中の背景に対する可読性・警告演出は今回未検証 |
| Title | 既存の開始・設定・終了と共通操作枠を維持 | 取得した正本では細かなTitle文言は現行UIに委譲。Online項目等を新仕様の必須項目と断定しない |
| Stage Select | Previous／Current／Next、端で停止、前回Stage、下部Mission Data、CLEAR／Lock、保存後出発 | 3Dジオラマ／SceneCapture未実装。現在はサムネイル方式で、DA_TestStageConfigのThumbnailもNone。StageCatalogは1件なので3Stage同時表示の実動作未確認 |
| Pause | 戻る・装備・設定・Lobbyの4項目、Retryなし、Lobby確認あり、子画面から復帰するコード | Pause中のゲーム進行停止・BGM継続の実動作は今回未検証 |
| Lobby確認 | 指定の警告と質問、はい／いいえ。既存Controllerへ接続 | 実行時のRun破棄・復帰は今回未検証 |
| Gadget Loadout | 4Slot、空にする候補、解放済みCatalogだけ表示、固定Catalog順、即時変更／保存、詳細欄 | 正本の初期装備はアミ＋剣だが、現在のBP_BoarGameInstance.GadgetCatalogはBP_NetGadgetのみ。剣はカタログ未登録。既存装備機能の仕様変更は今回行っていない |
| Settings | BGM／SE、戻る、図鑑導線。既存Audio Subsystemへ接続 | 音量はGameInstance内保持で再起動保存なし。取得したUI共通本文に設定項目全体の専用定義はなく、参考画像だけから機能不足を断定しない |
| Encyclopedia | Lobby／Settingsの入口、Saveの個体ID判定、未捕獲情報隠蔽、種類別／全体進捗のコード | BoarSpawnDefinitionsが空なので個体枠が生成されない。以前のユーザー方針どおりデータ未登録を維持。写真・説明等の登録と実データでの確認が必要。初捕獲Stage欄は個体定義の所属Stageを表示している |
| Result | 確定Runのクリア時間・現在収容数・NEW件数・コイン件数、累計図鑑なし、専用戻るButtonなし、任意入力・多重遷移防止のコード | 「今回NEWの個体」は件数のみで個体一覧を表示していない。コインも総件数表示で、個別NEW演出はない。正本どおりの個体情報表示は追加対応が必要 |
| GameOver | Resultとは別画面、Retry／Lobbyの接続、暗いPanel・赤警告 | Retry時の最新装備維持など実行時確認は今回未実施 |

取得した正本と完全一致とは判定しない。上記の機能・素材差分は今回の発光変更とは別の残作業。未登録の個体ID、Stage、剣定義、写真を推測で追加していない。

## 検証

- 15 Widgetを warnings_as_errors=true でCompile成功。
- 共有Material再Compile、変更15 Asset（Material 1、MI 12、Widget 2）のSave成功。
- 再取得で未保存Assetなし、15 Custom入力の接続、UI Domain／透過、Time IgnorePause、用途別パラメータ、GameOver／図鑑Brush参照を確認。
- 保存後の対象ログにShaderのCompile失敗なし。
- C++変更なし。今回はC++再ビルド不要。
- PIE・UE実行テストはユーザー方針により未実施。画面遷移、ゲームパッド、DPI変更、可読性、発光の動きは実行未確認。
