# Lobby退出確認UI

- ゲームループ仕様書のPause退出確認を実装。文言は「ステージ挑戦中の進行は破棄されます。Lobbyへ戻りますか？」。
- BoarConfirmationWidgetはYes/Noの選択通知のみ担当し、表示名はClass Defaultsで設定。画面遷移は既存Controllerが担当。
- LeaveStageFromPauseは即時退出から確認表示へ変更。「はい」の場合だけ既存ReturnToLobbyを呼び、Clear保存を呼ばずLevel遷移でStageRunDataを破棄。
- 「いいえ」または既存MenuBack入力はPauseへ戻り、LobbyボタンへFocusを復帰。確認中もPause・入力遮断を維持。
- 初期Focusは「いいえ」。初回レイアウト後に一度だけ再設定。多重生成・二重決定を防止し、DestructでDelegateを解除。
- 既存HoloPauseパネル、HoloMenuEntryボタンStyle、HoloActionFrameのアンバーFocus枠・拡大、既存Fontを再利用。
- `/Game/BP/Widget/WBP_LobbyExitConfirmation`：SafeZone、中央640×560、14要素。警告文のみ赤、通常シアン、Focusアンバー。既存Pauseの20要素と親Classは維持。
- C++ Development Editorビルド成功。確認Widget／Pause／PlayerControllerを警告もエラーとしてCompile成功。
- 対象3 AssetのSave成功、再取得でWidget Tree・Yes/No Binding・Controller Class参照を確認。全対象Dirty=false。
- ユーザー指定によりPIE・UE実行テストは行わない。
- 実行時の初期Focus、キャンセル復帰、時間停止、Lobby遷移、未確定進行の破棄は未確認。ユーザー側の実行確認対象。
