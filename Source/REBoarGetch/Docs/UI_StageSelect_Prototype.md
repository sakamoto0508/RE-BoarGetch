# ステージ選択UI Prototype — 2026-09-22

Notion: https://app.notion.com/p/3dfb6c88752581bf8e85e4c65fca9b37

## 実装範囲

- `/Game/BP/Widget/WBP_LobbyStageSelect` を新規作成。親は既存 `UBoarLobbyWidget`、19 Widgets。SafeZone → Canvas → 中央1000×640の半透明パネル。
- ステージ名・説明・捕獲目標・任意サムネイル・出撃・キャンセル。名前指定で既存C++の更新／クリック処理に接続。C++ソース変更なし。
- `/Game/BP/Widget/WBP_HoloActionFrame` を新規作成。NamedSlot内の実ボタンを親WidgetTreeに保持し、FocusPathでアンバー枠＋1.035倍拡大を表示。既存WBP_HoloMenuEntryとそのMaterialを再利用し、既存共通Widgetは変更していない。
- 新規 `/Game/UI/Prototype/MI_UI_HoloStageSelect_Panel_Prototype` は既存ホログラムMasterを継承。Width1000、Height640、Fill0.72。
- Constructの次TickにUIOnlyと出撃ボタンへのフォーカスを設定。マウスHoverもKeyboardFocusに接続。閉じる際のGameOnly復帰は既存StageEntrance処理。
- `/Game/BP/Lobby/BP_StageEntrance_Stage01` のlobbyWidgetClassを設定。Lobbyの仮Triggerと同位置(1210,0,130)に不可視の既存入口クラスを追加、範囲150×220×130。既存Trigger、3D造形、アセット名は保持。

## 検証

- Compile: WBP_LobbyStageSelect、WBP_HoloActionFrame、BP_StageEntrance_Stage01成功。
- Save成功。再取得でWidget数、6つの名前参照、配置入口のConfig/Class参照を確認。
- PIE: ロビーでテスト用にプレイヤーを入口外から内へ移動し、実Overlap経由で表示。
- DA_TestStageConfigのStage 1／3体を捕獲しよう／捕獲目標3を表示。
- 初期出撃Focus、アンバー太枠と拡大を画像確認。
- MCPのGamepad_DPad_Rightでキャンセルへ移動、Gamepad_FaceButton_Bottomで画面を閉じることを確認。
- 入口外→内で再表示、Enterで実際に `/Game/Level/Test` へ遷移。HUDのHP5/5、捕獲0/3を確認。
- PIE停止済み。物理ゲームパッドでの実機確認は未実施。

## 未実装・必要素材

- ConfigのthumbnailはNone。推測素材は設定せず、Imageは既存処理でCollapsed。割当後の画像表示は未検証。
- 今回は単一ステージ。Notionの複数ステージ切替、ロック状態、クリア状況、コイン／図鑑進捗、3Dミニチュアは次段階。
- キャンセルは画面の「ロビーへ戻る」を選択して決定。B/Escapeの直接キャンセルは追加していない。
- 初回検証で入口内に直接PIEスポーンするとUIは出なかった。通常の入口外からの侵入と再侵入を検証済み。

![PIE](UI_StageSelect_Prototype_PIE.png)
