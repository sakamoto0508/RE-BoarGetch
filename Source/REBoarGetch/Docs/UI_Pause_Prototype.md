# ポーズUI Prototype — 2026-09-22

- Notion参照: https://app.notion.com/p/3a3b6c8875258142a900d1033be885e5 、 https://app.notion.com/p/398b6c887525801ab875fda40262b95c
- 新規 WBP_PauseMenu（BoarPauseWidget基底、14 Widgets）、MI_UI_HoloPause_Panel_Prototype。既存ホログラムMaster、WBP_HoloActionFrame、ボタンMaterialを再利用。
- BP_BoarPlayerControllerのPauseWidgetClassへ接続。P／Gamepad_Special_Rightで開く。キーはController Class Defaultsで変更可能。
- 「ゲームに戻る」「ロビーへ戻る」。戻るボタン、P、メニューボタン、Escapeで再開（PIEではEscapeのEditor割当が優先される場合あり）。
- UIOnly／時間停止、閉じる際のGameOnly／カーソル非表示、入力フラグ復帰。プレイヤー状態GetterはMenuを返し、中断前の行動状態は保持。
- Playing以外、終了処理中、既に停止中、Class未設定の画面では新規表示しない。終了処理・Controller終了でもメニューを閉じる。
- 新規Widgetは開くたびに生成。初回UI TickのDoOnceで初期フォーカスを再設定。時間停止中にWorld Timerを使わず、以後フォーカスを奪わない。
- ロビー退出は既存ReturnToLobby経路を使用し、Clear保存処理を呼ばない。

## 検証結果

- Development Editor C++ビルド成功。Widget Compile・Controller Compile・Save成功。保存後にWidget数と名前参照を再取得。
- ユーザーが実行テスト不要と指定する前にPIEを実施。Pで表示、黄色太枠と拡大、十字キーで選択、ゲームパッド右FaceボタンとEnterで再開を確認。
- 停止中のBoar3体のTransformが複数回取得で一致。再開後に移動、カーソル非表示を確認。
- P／メニューボタンによる複数回開閉を実行。
- ロビー退出の入力送信後、MCP通信が切断したため、ロビー到着・退出後の入力復帰は未確認。原因は未調査で、正常完了やクラッシュとは断定しない。
- 物理ゲームパッド、移動・ジャンプ・ガジェット・終了処理との境界、複数解像度は未確認。
- ユーザー指示: 今後UE上の実行テスト・PIE確認はユーザーが担当する。こちらはCompile・Save・再取得まで。

## 次段階

GameOver専用UI（Retry／Lobby）の接続を推奨。Pause内の装備変更画面は別作業。
