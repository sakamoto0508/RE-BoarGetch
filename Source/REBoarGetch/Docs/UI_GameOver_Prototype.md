# GameOver UI Prototype

## 仕様と変更範囲

- 正本: [ゲームループ仕様書](https://app.notion.com/p/398b6c887525803b93dedbd9f5231423)、[世界観・UI](https://app.notion.com/p/3dcb6c887525810faf13d77dc129f8f4)。CRT表現との相違は、ユーザー指定の透明ホログラムを優先。
- 作業前: GameOverアセットなし、BP_BoarPlayerController.GameOverWidgetClassはNone。既存UBoarGameOverWidgetにはボタン名解決、Retry/Lobbyイベント、初期フォーカス処理あり。
- `/Game/BP/Widget/WBP_GameOver` を新規作成。親は既存BoarGameOverWidget。SafeZone → Canvas → 中央640×560パネル、15 Widgets。
- 既存PauseパネルBrush、WBP_HoloActionFrame、ボタンMaterial、Robotoを再利用。新規Texture/Icon/Font/Materialなし。
- GameOver見出しと細い警告線だけ赤。パネルは半透明シアン、選択はアンバー太枠・発光・1.035倍拡大。
- Button_Retry／Button_LobbyをClass DefaultsのretryButtonWidgetName／lobbyButtonWidgetNameへ設定。クリックは既存C++イベントを使用。
- 初回UI TickのDoOnceでRetryフォーカスを設定。Hoverも同じフォーカスへ接続。
- BP_BoarPlayerController.GameOverWidgetClassへWBP_GameOver_Cを設定。他のController参照、3D世界、既存C++処理は変更していない。

## 検証

- Widget Compile成功。Controller Compileはエラーなく完了。両アセットSave成功。
- 再取得で親クラス、15 Widgets、2ボタン名、GameOverWidgetClass設定を確認。
- ユーザー指定によりPIE・UE実行テストは未実施。表示、ゲームパッド操作、HP0からの遷移、Retry後の初期化、Lobby復帰はユーザー確認対象。

## 関連する仕様差分（今回変更なし）

- 今回取得したゲームループ仕様ではPauseのトップ項目に装備・設定、Lobby退出時の確認ダイアログ、Esc/Menuでの表示がある。既存Pause Prototypeの2項目・Pキー・直接退出との差分は別作業。
