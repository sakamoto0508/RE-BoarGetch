# Settings prototype — Compile / Save / 再取得済み

## 実装済みのC++基盤

- UBoarSettingsWidgetを追加。既存BoarAudioManagerSubsystemのBGM/SE音量を使用。
- 0〜1のSlider、0〜100の数値表示。StepSizeは0.05。
- Constructで現在値を読み込み、初期化では音量を変更しない。
- DelegateはDestructで解除。Audio Managerがなければ調整を無効化。
- OnClosedは呼び出し元へ通知するだけで、Pause解除・入力モード変更を行わない。
- 音量は既存仕様どおりGameInstanceの存続中のみ保持。アプリ再起動後への保存は未実装。
- SE変更はManager経由で今後再生するSEが対象。

## 2026-09-23の接続・画面実装

- Unreal MCP再接続後、Notionアート・ゲームループ仕様とTitle/Pause構造を取得。CRT表現よりユーザー指定の透明ホログラムを優先。
- `/Game/BP/Widget/WBP_Settings`：BoarSettingsWidget派生、SafeZone、中央640×560、18要素。BGM/SEスライダーと数値、「戻る」。
- 既存PauseパネルMaterial、HoloActionFrame、HUDのFont、MenuEntryのButton Styleを再利用。スライダーはController Lock不要で左右調整。Focusは既存アンバー枠＋拡大。
- TitleのSettingsEntryを有効化。既存親Class・開始／終了イベントを維持してOpenSettingsMenuを接続。
- PauseにSettingsFrame／Button_Settingsを追加（20要素）。4項目用パネルMaterial Instanceを追加し、既存GameOver共用Materialへの影響を避けた。
- Controllerが設定画面の多重生成を防止。親画面を一時非表示にし、戻ると元のVisibilityと設定ボタンFocusを復帰。TitleはUIOnly、PauseはGameAndUI・Pause状態を維持。
- Pauseの既存MenuBackは設定を先に閉じる。Titleからは画面内「戻る」を使用。
- 初回レイアウト後に一度だけFocusを設定。音量の毎Tick Bindingは追加しない。

## 検証

- Development Editor C++ビルド成功。
- Settings／Title／Pause／両Controller Blueprintを警告もエラーとしてCompile成功。
- 対象AssetのSave成功、再取得でClass参照・Binding・Widget Treeを確認。対象6 AssetのDirty=false。
- PIE・UE実行テストはユーザー指定により未実施。実際の音量反映、Gamepad遷移、Focus復帰、可読性は未確認。
- 次の候補は仕様必須のLobby退出確認ダイアログ（まだ未実装）。
