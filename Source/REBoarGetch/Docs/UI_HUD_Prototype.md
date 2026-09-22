# WBP_PlayerHUD ホログラムPrototype

対象: `/Game/BP/Widget/WBP_PlayerHUD`。Unreal MCPで編集。

## 正本と今回の方針

[Notion UI仕様書](https://app.notion.com/p/3dcb6c887525810faf13d77dc129f8f4)のUI基準・Codex実装ルールを取得して確認。NotionにはCRT端末方針が残るため、今回のユーザー指示に従ってこのHUDのみ透明ホログラム化した。Notionページ自体は変更していない。参考画像は会話に届いておらず未参照。

## 変更

- 親 `BoarHUDWidget`、既存26Widget、参照名、EventGraph、C++更新処理、入力設定を保持。
- HP: 左上Anchor、40px余白、344×112の横長ステータスパネル。既存の現在HP／最大HP TextBlockを使用。残量連動ゲージではない。
- 捕獲数: 右上Anchor、40px余白、400×112。現在収容数／目標数を明示。
- Gadget: 右下Anchor、48px余白。252×252の既存十字4枠を維持。選択ImageにアンバーのGlow・太枠・1.08倍拡大。
- 背景Imageと小見出しを2個ずつ追加、合計30Widget。画面中央には追加なし。
- 既存RobotoとGadget画像更新を再利用。未確定Texture/Icon/Fontの追加なし。
- 新規 `/Game/UI/Prototype/M_UI_HoloHUD_Prototype` とHP/Capture/Slot/SelectedのMaterial Instance 4個。UIドメイン・半透明、切り欠き枠、薄いグリッド・走査線。Width/Height/Fill/Thickness/Glow/Tintを調整可能。正式な全画面共通StyleではなくPrototype。
- 赤警告を必要とする既存表示／イベントはHUDにないため、警告ロジックは追加していない。

## 入力確認

実IMC_Default: RB＋十字左/上/右/下がSlot1/2/3/4。Action名のNorth/Westだけでは方向を判断しない。キーボードはCtrl＋1/3/4/2がSlot1/2/3/4に対応する既存設定で、今回は変更していない。HUD自体は非Focusableで、選択ImageのVisibilityを既存イベントが更新する。

## 検証

- UI Material compile成功。
- Widget Compile=true、対象WidgetとMaterial 5資産のSave=true。
- Save後再取得: 親Class・既存26部品保持、30部品、Class Defaultsの参照設定は変更前と一致。Material参照とAnchorを確認。
- `/Game/Level/Test`でPIEを起動し、HP 5 / 5、収容0 / 3、4枠・NetGadgetアイコン・初期選択の黄色太枠を確認。半透明面、中央視界、可読性、端の余白を1280×720相当の描画で確認。PIE終了済み。
- 未確認: 被弾／捕獲／解放による数値変化、複数装備の切替、実機Gamepad、他解像度・プラットフォームSafe Zone。Compile成功とこれらの動作保証を混同しない。
- C++・Level・既存Input・Gadget DataAssetは無変更。

変更前プロパティ: [UI_HUD_Prototype_Before.json](UI_HUD_Prototype_Before.json)。PIE画像: [UI_HUD_Prototype_PIE.png](UI_HUD_Prototype_PIE.png)。
