# Result / Title ホログラムPrototype

前回取得したNotion仕様と現在の実装を参照し、今回もUIの透明ホログラム方針を適用。C++、入力Mapping、3D配置・Lighting、プロジェクトの開始Mapは変更していない。

## Result

- 既存 `/Game/BP/Widget/WBP_Result` を編集。親 `BoarResultWidget` と既存11部品、Constructグラフ、現在収容数／目標数の参照、任意入力によるDismiss経路を保持。
- 中央760×640の半透明シアンパネル、アンバーのSTAGE CLEAR、青白い数値・案内へ変更。
- 現在収容数／目標数の小見出しと、クリアタイム・NEW新規登録数・特別コインの3行を追加し、計21部品。既存 `InitializeRunResult` が参照するClass Defaultsへ追加TextBlock名を設定。新たなTick／Bindingは追加していない。
- 戻る案内は「任意のボタンでロビーへ戻る」。既存autoWrapTextを無効化し、一文字だけ折り返す現象を修正。
- `/Game/UI/Prototype/MI_UI_HoloResult_Panel_Prototype` を追加。共通Materialを再利用し、Width=760、Height=640、Fill=0.72。

## Title

- 新規 `/Game/BP/Widget/WBP_Title`（親BoarHUDWidget、SafeZone、17部品）。既存ControllerのHUD生成・破棄経路を再利用。
- メニューは「ゲーム開始／オンラインプレイ／設定／ゲーム終了」。開始は `/Game/Level/L_Lobby`、終了はQuitGameに接続。未実装のオンライン・設定は無効化して「準備中」と明示。
- 新規 `WBP_HoloMenuEntry` は3部品の共通メニュー。FocusPathの出入りでアンバー枠と1.035倍拡大を更新。Hover時もKeyboardFocusを設定し、ClickはOnInvokedで親へ通知。毎Tick監視なし。
- 通常・選択用Material Instanceを2個追加。既存Robotoを再利用し、Texture／Icon／Fontは追加していない。
- 新規 `/Game/BP/Title/BP_PC_Title` と `BP_GM_Title` を既存 `/Game/Level/Title` のGameMode Overrideへ接続。Mapには既存3D背景がないため、現在は黒背景。3D背景の作成は未実施。
- `GameDefaultMap`は変更していない。Titleは `/Game/Level/Title` を開いてPlayすると確認できる。

## 検証の範囲

- Material Instance、Widget、関連BlueprintのCompile／Save成功。保存後の再取得で親Class、部品数、追加数値参照、Title GameMode接続を確認。
- Title PIE: 初期Focus、上下キー移動、無効項目のスキップ、アンバー枠・拡大、Enterでロビー遷移と既存LobbyHUD表示を確認。
- Title PIE: MCPからGamepad_DPad_Down、Gamepad_FaceButton_Bottomを送信し、終了項目へのFocus移動とPIE終了を確認。実機Gamepadの確認とは区別する。
- Result PIE: 一時的な直接表示でレイアウト・日本語・案内の一行表示を確認。画像の0/0は既存Constructの初期値、他の「—」は未投入値。実クリア結果のサンプルではない。
- Result確認用のCreateWidget／AddToViewportは削除し、Title ControllerのHUD参照を復元後、Compile／Save・再取得済み。一時表示処理は残していない。
- 未確認: 実クリア→保存→Result→ロビーの通し動作、実結果データの各値、実機Gamepad、複数解像度。オンライン／設定機能、Title用3D背景は未実装。
- PIE終了済み。既存のGameInstance・Result接続など別途未完了のゲーム進行設定は今回変更していない。

画像: [Result](UI_Result_Prototype_PIE.png)、[Title](UI_Title_Prototype_PIE.png)。
