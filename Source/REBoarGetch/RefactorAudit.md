# RE-BoarGetch 設計監査 (2026-10-02)

## Phase 0 — 変更前の実測

- 作業ツリー: main / 184aa6e13cb0f153b33434b8fe4b74959f43bacf、変更なし。
- git ls-remoteでremote mainも同一SHAと確認。
- UE 5.8 Editor (PID 8100)、現在Level /Game/Level/Stage1、PIE停止中、Level dirty=false。
- 開いているAsset: WBP_ArchiveDisplaySelection / DA_TestStageConfig / ABP_NormalBoar / AS_BoarWalk。
- Editor用Developmentビルド成功（変更前はup-to-date）。既存REBoarGetch.Spec 5件成功。
- AssetToolsで実Assetを列挙、Loadout/Lobby Widgetの依存を取得。両WidgetともScript/REBoarGetchの派生、既存表示MaterialとEntry Widgetに依存。
- DataAsset / GameInstance BPの実プロパティを確認。既存Assetと保存フォーマットは変更対象にしない。
- 適用するAGENTS.mdは見つからなかった。

## 責務と変更判断

| Class | 現在の責務・依存先 | 問題 | 最小限の改善 | 優先度 |
|---|---|---|---|---|
| BoarLoadoutWidget | 表示、Focus、Catalog検証、GIのUnlock/Test操作、GadgetComponent装備、Facility判定、Save結果文言 | ViewがModel詳細と操作規則を知る | Presenter一つに操作・表示データ構築を移す。Widgetは描画とFocusを維持 | 最優先/Phase 1 |
| BoarLobbyWidget | Stage一覧、SaveのLastAttempted/Clear/Coin/捕獲状態、StageConfig、Preview生成、Controllerへの通知 | 永続進行を直接解析する | 今回の必須Loadout変更から分離。将来Lobby Presenter候補として残す | 中 |
| BoarGameInstance | Save候補の複製・保存・公開、Stage条件、Archive、Gadget Catalog/Unlock/Test/施設条件 | Gadget規則と永続化Coordinatorが集中 | Gadget系Subsystem一つ。GIのBP API/Catalog設定/Delegateは互換入口として残す | 高/Phase 2 |
| BoarGameMode | Stage初期化、終了優先判定、Save/Result、Spawn、捕獲時Cage探索/Drop、Actor停止 | 捕獲の物理後処理がStage全体ルールに混在 | Capture解決をprivate helperへ抽出。Spawnは既存private helperを維持、停止Interfaceは導入しない | 中/Phase 3 |
| BoarPlayerController | Input、HUD、Result/GameOver、MappingContext、Pause/Settings/Loadout/図鑑/施設/Confirmation | 約1000行、メニュー生命周期の独立した変更理由 | メニューFlowをComponentへ移す。既存Controller API/BP設定は維持、Result/GameOverはControllerに残す | 高/Phase 4 |
| GadgetComponent | 4Slot/Use/装備Actor、Save復元と即時保存、Stage/Facility判定 | Test世界判定と装備初期化方針をComponentが知る | 外側Coordinatorから初期Loadout/一時装備方針を渡す。初期化順維持 | 中/Phase 5 |

## 維持する契約

- StageConfig=静的設定 / StageRunData=挑戦中状態 / BoarSaveGame=永続進行。
- SaveCandidateの保存成功後公開、読込失敗時上書き禁止、Coin非消費、初期Unlock、4Slot/空Slot/重複Gadget移動/保存再試行。
- Gadget Testはセッション内選択と一時装備。通常Saveを変更しない。
- Labだけ手動Unlock/Test選択。Pauseは装備変更のみ。CatalogのID重複除外と固定順。
- Clear > GameOver、WorldTickEndの確定、Capture Presentation待ち、Cage集計順序。
- UFUNCTION/編集可能UPROPERTY/Blueprintイベント・Assetパスは保持。private transientの内部参照だけ整理可能。
- AGadgetBase/NetGadgetの使用契約に今回変更が必要なLSP違反は認められず、変更しない。

## 各Phaseの検証方針

C++ビルド成功を次Phaseの条件とする。新binaryをロードしたEditor/commandletで既存Testと追加Test、関連BP Compile、可能なPIEを確認する。実ユーザーのSaveへの書込を検証のために行わない。未実施の確認は明記する。
