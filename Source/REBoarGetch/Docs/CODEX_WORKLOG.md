# RE:BoarGetch Codex Worklog

最終更新: 2026-09-15

## このファイルの目的

別のCodexタスクや新しいチャットで作業を再開するときの引き継ぎ資料です。
最初にこのファイルを読み、完了済みの作業を重複して実装せず、未完了項目と次の作業候補を確認してください。

このファイルには、次の確認方法を区別して記録しています。

- **コード確認済み**: `Source/REBoarGetch` のC++で存在を確認したもの
- **ユーザーPIE確認済み**: Unreal Editor上のBlueprintやアセットを含め、ユーザーが実動作を確認したもの
- **MCP／画面確認済み**: Unreal MCPでアセットや実行時状態を取得し、必要に応じてPIE画面でも確認したもの

Blueprint、Animation Blueprint、MontageなどのContentアセットは、このSourceディレクトリだけでは内容を完全に検査できません。ユーザーPIE確認済みの記録を現状の根拠として扱ってください。

### ドキュメントの役割

- このファイル（`Source/REBoarGetch/Docs/CODEX_WORKLOG.md`）をCodex作業ログの正本として扱う
- **Notion「世界観・アートディレクション／UIデザイン仕様書」**: 世界観、ステージ、環境、UIデザイン基準の正本
- **Notion「ゲームループ仕様書」**: 画面遷移、ゲーム進行、リザルト表示項目の正本
- **Notion「Memo」**: C++クラスの責務、公開API、連携先の説明
- **Notion「仕様書タスク（Private）」**: 個別機能の受け入れ条件と進捗
- **本ファイル**: 実装履歴、ビルド／PIE結果、未確認事項、次作業の引き継ぎ

仕様変更はNotionへ記録し、本ファイルには仕様全文を重複掲載せず、実装に必要な要約と検証状態を残します。

プロジェクト直下とSource側に分散していたログを2026-09-15に照合し、本書へ現行要約を統合しました。両方の原文は `Docs/History/` に保存しています。過去の設定手順・ビルド結果は当時の記録であり、現在の完了根拠として読み替えないでください。

### アートディレクション要約（2026-09-15確定）

コアテーマは、**「2000年前後の少年が思い描いた、夏休みの明るい近未来冒険」**です。

- 青空、入道雲、強い夏の日差し、秘密基地、捕獲研究施設を基調とする
- 1998～2004年頃の少年向け玩具、家庭用ゲーム機、CRT、少し古い未来予想図から着想する
- 白・青・濃紺をベースに、シアン・黄色・ライムグリーンを主アクセントとして使用する
- 丸く厚い成形プラスチック、カプセル形状、大きな物理ボタン、LED、太いケーブルを優先する
- 現代的な暗いサイバーパンク、紫・ピンク中心、軍事SF、細線と六角形だけの汎用SF、全面ホログラムは避ける
- 特定作品のキャラクター、ステージ、UI、ロゴ、ガジェットを直接コピーしない
- 判断基準は「夏休み中の少年が見て触ってみたいと思うか？」とする

UIは**「携帯ゲーム機 × 捕獲装置 × 研究端末 × ブラウン管テレビ」**として設計します。日本語と可変値はTextBlock、フレームとアイコンは画像素材へ分離し、固定名BindWidgetへの強い依存を避けます。選択状態は色だけでなく、拡大、せり出し、発光、太いアウトライン、LED、アニメーションで示します。

詳細仕様: [世界観・アートディレクション／UIデザイン仕様書](https://app.notion.com/p/3dcb6c887525810faf13d77dc129f8f4)

---

## 現在地（2026-09-15整理）

今回の整理ではドキュメントと関連C++を照合しました。Unrealアセットの再取得、ビルド、PIEは実施していません。以下のPIE結果は過去ログに記録されたものです。

| 機能 | 確認済みの範囲 | 残っている確認 |
| --- | --- | --- |
| プレイヤー | Locomotion、二段ジャンプ、アミMontage、捕獲判定窓、使用中ロックと復帰のユーザーPIE記録あり | Stun Montage、Capture/Menu状態の表示・通知 |
| イノシシ・檻 | 巡回、逃走、檻攻撃、捕獲・収容、檻破壊・解放・AI復帰、檻Respawnの過去確認記録あり | 新ステージでの回帰確認 |
| 4スロットHUD | C++イベント通知、2026-09-13に4枠・空枠・アミ画像・初期選択枠のMCP/PIE確認 | 実機R1＋十字キー、複数装備切替、空スロット入力。Cooldown表示は後回し |
| Audio Manager | 2026-09-14にC++追加、Editorビルド成功記録 | Sound接続、音量、BGM遷移継続のPIE |
| StageConfig／StageEntrance | 表示情報・Level参照・入口Overlap・決定／キャンセルのC++あり。追加時のビルド成功記録 | 実アセット設定とロビー→Test→ロビーの通しPIE |
| 出現数と目標数 | BoarSpawnEntries、BoarSpawnPoint、開始時生成のC++あり | 最終リンク、5体生成・3体捕獲クリアのPIE |
| リザルト | 任意入力→ロビーのC++あり。2026-09-14にWBP_ResultのButton除去・ガイド追加・Compile/Save記録 | 変更後の最終リンクと任意入力→ロビーのPIE |
| ロビー空間 | 2026-09-13にMap Check 0 Error/Warning、開始・前進・ジャンプ確認 | 全域導線・外周、最新世界観に合わせた再制作 |
| 新しいステージ・ロビーUIデザイン | Notionに世界観と制作基準を登録 | 既存アセット調査、設計、ブロックアウト、実制作 |

過去のRetry動作確認は旧仕様の検証です。現行リザルトのロビー復帰確認には含めません。個別ビルド成功後にも変更があるため、全変更の最終リンク成功とは扱いません。

## 現行の接続・設定

### StageConfigと出現数

- `StageId`、`DisplayName`、`Description`、`Level`、`Thumbnail`、`TargetCaptureCount`、`BoarSpawnEntries`を保持。
- `TargetCaptureCount`はクリア条件専用。例：目標3、Spawn EntryのCount 5。
- `BoarSpawnEntries`が空なら自動生成しない。既存の手置きBoarを利用する。
- `ABoarSpawnPoint`は生成位置と向き。生成数が地点数を超えると地点を循環使用する。
- 設定出現数を目標が超える場合はWarning。手置きと自動生成を混在させる際は実際の総数を確認する。
- `/Game/Level/Test`はテスト先候補。正式Stage 1のLevel名・設定完了を推測しない。

関連：[StageConfig.h](../Public/Stage/StageConfig.h)、[StageEntrance.cpp](../Private/Stage/StageEntrance.cpp)

### StageEntranceとLobby Widget

- `AStageEntrance`はローカルPawnのOverlapで単一のStageConfigを`UBoarLobbyWidget`へ渡す。
- 決定時は同じStageConfigか確認し、多重要求を防いで設定Levelを開く。
- 現行C++は`Game And UI`入力。明示的なプレイヤー移動停止は実装されていない。
- キャンセル／退出時はWidgetとDelegateを解除し、Game Only・カーソル非表示へ戻す。
- Class Defaultsの任意Widget名で、ステージ名・説明・目標TextBlock、Thumbnail Image、開始／キャンセルButtonを解決。
- 左右切替、ステージ一覧、ロック判定、ベストタイム、ミッション、クリア状態表示は完成デザイン側の追加要件。現在の単一ステージUIとは区別する。
- 静止プレビューを先行する方式とA/B・LB/RB割り当ては提案。正式採用および実機確認は未記録。

関連：[BoarLobbyWidget.h](../Public/UI/BoarLobbyWidget.h)

### リザルト

- `UBoarResultWidget::InitializeResult()`で捕獲数と目標数を表示。
- `FocusForDismissInput()`でWidgetへFocus。キー／マウスボタン入力を`OnDismissRequested`で通知。
- Controllerが一度だけ`LobbyLevelName`へ遷移（C++既定値：`L_Lobby`）。
- Designerには捕獲数・目標数TextBlockと「いずれかのボタンを押してロビーへ戻る」ガイドを配置。Retry／Title Buttonは現行設定に不要。
- クリアタイム、ミッション、コイン、特別コイン、解放内容はゲームループ仕様上の表示要件。現在のResult APIで反映済みとは扱わない。
- 檻破壊をGameOver条件とする過去生成画像は採用仕様の根拠にしない。既存の檻破壊・解放・Respawnとの整合確認が必要。

関連：[BoarResultWidget.cpp](../Private/UI/BoarResultWidget.cpp)、[BoarPlayerController.cpp](../Private/Player/BoarPlayerController.cpp)

### 4スロットHUD

- `UGadgetComponent::OnGadgetLoadoutChanged`をControllerが購読し、`UpdateGadgetSlots()`へ転送。初期表示時も現在値を反映。
- 空背景は常時表示。装備済み枠にDefinitionの`DisplayIcon`、選択中だけアウトラインを表示。
- Class Defaultsの`Gadget Icon Widget Names`／`Gadget Selection Widget Names`へSlot 1～4の任意名を登録。
- 2026-09-13記録：Slot 1=左、2=上、3=右、4=下。初期装備はSlot 1の網のみ。
- 同日R1＋十字キーのMapping追加記録あり。ログ内の「Mappingなし」は追加前の状態。実機受け入れは未確認。

## 次の作業と未確定事項

1. ステージ／UI制作に着手するタスクでUnreal MCPからLevel、既存アセット、Widget、移動・ジャンプ性能、StageConfig、StageEntrance、Boar、Cageを再取得する。
2. 新世界観に沿って構成案・導線を決め、ブロックアウト→PIE移動確認→装飾→Lighting→最終PIEの順で進める。
3. UIは現状調査→構成案→共通部品→Title→LobbyHUD→StageSelect→状態差分→ゲームパッド→Compile/Save→再取得の順。
4. リザルト復帰、5体生成・3体クリア、4スロット切替を回帰確認する。
5. ユーザーPIE報告後にNotionタスク表を再取得し、受け入れ条件を満たしたものだけDoneにする。

タイトルの現行仕様項目は「ゲーム開始／オンラインプレイ／設定／ゲーム終了」。参考画像の「ステージセレクト／クレジット」は仮表示で、正式なメニュー変更・機能実装を意味しません。

### その他の継続事項

- 約20 FPSの性能調査は未確定。前面PIEでstat unit・GPU/CPU内訳を採取し、初回シェーダー生成と定常負荷を区別する。未計測の候補だけで設定を変更しない。
- Stun Montageは後回し。Menu状態・行動状態通知・Automation Testは完了記録なし。
- ゲームループの遷移共通化、ResultData拡張は今後の候補。
- 既存アセットの削除・置換・改名はユーザーの指示範囲を確認する。

## 履歴と参照

- [Source側原文の保存版](History/SOURCE_WORKLOG_2026-09-15.md)：Locomotion、アミ、入力、HUD、ロビー、Audio、性能調査などの詳細。
- [プロジェクト直下原文の保存版](History/PROJECT_WORKLOG_2026-09-15.md)：AI／檻／捕獲履歴、StageEntrance、リザルト変更、Boar出現設定などの詳細。
- プロジェクト直下の元ログは保存版と同内容を確認したうえで参照案内へ切り替える。
- [ゲームループ仕様書](https://app.notion.com/p/398b6c887525803b93dedbd9f5231423)
- [Memo](https://app.notion.com/p/3a8b6c88752580e392dbfcd09c33461f)
- [仕様書タスク](https://app.notion.com/p/07318d6af51b46b4b7f93ea2299bc148)

## 今後の更新ルール

現行要約を直接更新し、旧手順は日付付き履歴へ残します。コード確認・アセット取得・ビルド・ユーザーPIE・MCPのPIEを区別し、未確認は明示します。本書とNotionの変更後は再読込して確認します。

### 2026-09-15 整理記録

分散ログを履歴として保存し、現行手順を本書へ統合。Retry/Title手順を履歴へ移し、Notion Memoに現行クラス説明を設置。デザイン目標・提案・実装確認を区別しました。C++・Content変更、ビルド、PIE、タスクDone更新は行っていません。
