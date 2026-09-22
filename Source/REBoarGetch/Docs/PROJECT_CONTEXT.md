# RE:BoarGetch — Astra / Codex 作業コンテキスト

## 概要

Unreal Engine 5.8製の3Dアクション。ガジェットでイノシシを捕獲し、檻を防衛する。ClearはResult→Lobby、GameOverは専用UI→Retry／Lobbyへ進む仕様。
プロジェクトは `REBoarGetch.uproject`、主なC++は `Source/REBoarGetch/`。本書は作業開始用の現状要約であり、実装・動作を保証するものではない。

## 仕様の正本

MarkdownとNotionが矛盾する場合はNotionを優先する。実装との差は未対応として扱い、コードやアセットが仕様どおりと推測しない。Notionを取得できない場合は未確認と報告する。

- 世界観・アート・UI：[世界観・アートディレクション／UIデザイン仕様書](https://app.notion.com/p/3dcb6c887525810faf13d77dc129f8f4)
- ゲーム進行・画面遷移：[ゲームループ仕様書](https://app.notion.com/p/398b6c887525803b93dedbd9f5231423)
- C++クラス責務：[Memo](https://app.notion.com/p/3a8b6c88752580e392dbfcd09c33461f)
- 実装タスク・受け入れ条件：[仕様書タスク（Private）](https://app.notion.com/p/07318d6af51b46b4b7f93ea2299bc148)

## 作業ルール

- 前回の作業状態を推測しない。作業前に実際のコード・Level・Blueprint・Widget・DataAsset等を確認する。
- Blueprint / AnimBP / DataAsset / Mapの状態をC++だけから推測しない。
- ユーザーの既存変更を保持する。既存アセットを勝手に削除・置換・改名しない。
- 新規作成前に同名アセットの有無・内容を確認し、重複実装を避ける。
- 変更後は可能な範囲で Compile → Save → 再取得 → PIE確認を行う。
- コード確認、アセット確認、ビルド、PIE結果を区別する。確認できなかったものは「未確認」とする。
- コードコメントと作業報告は日本語で行う。
- UIはイベント駆動を基本とし、固定名BindWidgetへの強い依存を避け、Class Defaultsから参照・素材を設定する。
- ステージは調査・導線設計・ブロックアウト・PIE移動確認を経て装飾する。

## 主要システム

以下はC++と既存記録の要約。アセット接続・PIE済みという意味ではない。作業対象は開始時に再確認する。

- プレイヤー：移動、ダッシュ、二段ジャンプ、HP・被弾・行動ロック、アミMontageと捕獲判定窓。基本動作のPIE確認記録あり。
- イノシシ・檻：StateTreeの巡回・逃走・檻攻撃、捕獲・収容、檻破壊・解放・AI復帰・Respawn。PIE確認記録あり。
- HUD：HP・捕獲数・ガジェット4枠をイベント更新。空枠・装備画像・初期選択枠の確認記録あり。
- StageConfig／BoarSpawnPoint：固定個体ID・SpawnPoint IDによる新生成方式と旧Class/Count方式を併存。出現数と現在収容数のクリア目標は独立。コイン定義・解放条件も追加。
- GameMode／StageRunData：最寄り正常Cageへの収容、全破壊時の即解放、現在収容数更新、フレーム終端のClear優先判定、終了時停止をC++実装。
- GameInstance／SaveGame：今回の挑戦と永続進行を分離。NEWを保存前に固定し、Clear保存成功後だけResultへ進む。装備・最終Stageは個別保存。
- StageEntrance／Lobby Widget：単一ステージの入口Overlap・表示・決定・キャンセル。GameInstance使用時は解放判定・最終Stage保存。表示中の移動停止は未対応。
- Result／GameOver：Resultへ確定Runを渡し、任意入力でLobbyへ戻る。GameOver専用Widget基底とRetryのMap再読込を追加。Widget実体の接続は未完了。
- Audio Manager：GameInstanceSubsystemでBGM・SE・音量を管理。

## 未完成・未確認

- 最優先：GameInstanceClass登録（現行DefaultEngine.iniに指定なし）、GadgetCatalogと固定ID設定、GameOverWidgetClassとResult追加表示の接続。未設定ではClear保存→ResultやGameOver操作が完了しない。
- ロビー→ステージ→Clear／GameOver→Retry／ロビーの通しPIE、5体生成・3体収容クリア、複数檻・破壊・再捕獲、実セーブ書込／再起動読込。
- Development Editorビルドと自動テスト3件（終了優先順位、NEW／メモリ保存復元、出現定義）は成功。上記の統合PIEを保証しない。
- 4スロットの実機入力・複数装備切替・空スロット入力。Cooldown表示は後回し。
- Sound接続、音量変更、Level遷移中のBGM継続のPIE。
- Pause／装備UI、複数ステージ切替・ロック表示・最終Stage初期選択・回転3Dミニチュア、図鑑、剣・スピードブーツは未実装。ミッション・解放演出は将来項目。
- コインBPの取得判定、開始／終了演出、檻の自然回復値と既存BPとの重複確認。GameFeatureDataのAsset Manager設定エラーをテスト起動時に検出、未対応。
- 新世界観に沿ったステージ・ロビーUI制作、ロビー全域の導線・外周確認。正式Stage 1やロビーの使用Mapは実参照で確認する。
- Stun Montage、Capture/Menu状態の表示・通知。性能問題の原因は未確定で、再現計測が必要。
- 生成画像のメニュー・失敗条件・3Dプレビューは採用済み仕様や実装完了の根拠にしない。

## 世界観

「2000年前後の少年が思い描いた、夏休みの明るい近未来冒険」。青空・入道雲・秘密基地・捕獲研究所、白／青／濃紺にシアン・黄色・ライム。丸く厚い樹脂、CRT、大きな物理ボタンを使う。暗い都市型サイバーパンクや全面ホログラムを避け、特定作品を直接模倣しない。判断基準は「少年が触ってみたくなるか」。

## 作業開始手順

1. 本書、ユーザーの最新依頼、対象Notion仕様・タスクを確認する。履歴を一括で読み込まない。
2. リポジトリ・ブランチ・差分と既存変更を確認し、対象コードを読む。
3. Unreal作業ではMCP／Editorの接続・PIE状態、現在Level、実アセットと参照関係を取得する。
4. 確認結果と未確認事項を整理して作業し、変更後に対象の受け入れ条件を検証する。

## 更新ルール

現在の状態が変わった箇所を置き換え、解消した未完了項目を取り除く。目安80行以内とし、日付付き作業履歴・一時的なビルドエラー・コードの細部・仕様全文を追記しない。履歴はGit、詳細仕様・タスクはNotionに残す。C++責務の変更はMemoへ反映し、PIE報告後は受け入れ条件を満たしたタスクだけDoneにする。
