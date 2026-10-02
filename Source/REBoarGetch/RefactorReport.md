# RE-BoarGetch 設計リファクタリング結果

2026-10-02 / UE 5.8 / 比較元 main `184aa6e13cb0f153b33434b8fe4b74959f43bacf`。
Phase 0の実測は [RefactorAudit.md](RefactorAudit.md)。各Phaseのビルド・テストを確認してから次Phaseへ進んだ。

## 1. Refactor前の主要問題

- `BoarLoadoutWidget`: 描画・Focusに加え、Catalog検証、Unlock条件、試用選択、装備変更、Save結果判定を持ち、GI/Component/Facility/DataAssetに直接依存していた。
- `BoarGameInstance`: Stage/Archiveの永続化とGadgetのCatalog/Unlock/施設制約/試用管理が集中していた。
- `BoarGameMode`: Stage終了規則と、捕獲時のCage探索・収納/Dropの後処理が混在していた。
- `BoarPlayerController`: Input/HUD/Resultに、Pause/Settings/Loadout/施設/確認画面の生成・Focus・終了処理が集中していた。
- `GadgetComponent`: 装備操作に加えてGI/Save/GM/施設種別を知り、復元と試用方針を決めていた。

## 2. 新しい依存構造

```text
BoarLoadoutWidget (描画・Focus・操作通知)
  └─ BoarLoadoutPresenter (選択状態・操作・表示データ)
       ├─ BoarGadgetSubsystem (Catalog・Unlock・試用・Loadout ID規則)
       │    └─ BoarGameInstance (保存成功後にSave候補を公開)
       │         └─ BoarSaveGame
       └─ GadgetComponent (Slot・装備Actor・Use)

BoarPlayerController (Input/HUD/Result・既存BP API/設定)
  └─ BoarUIFlowComponent (メニュー生命周期・Focus・入力Lock)

BoarGameMode (Stage規則・終了確定)
  └─ BoarCaptureResolution (Cage選択・捕獲後処理)

BoarGadgetLoadoutInitializer (Stage/Facilityの初期化調整)
  └─ GadgetComponent.InitializeResolvedLoadout (初期装備・一時装備方針を受領)
```

Subsystemは既存GIのCatalog設定と保存境界を参照する。GIの旧Gadget APIはSubsystemへの互換窓口なので、この境界は完全な一方向依存ではない。UIFlowもController設定・入力処理を利用する。

## 3. 変更したClass / File

### 新規

- `Public/UI/BoarLoadoutPresenter.h`, `Private/UI/BoarLoadoutPresenter.cpp`
- `Public/Gadget/BoarGadgetSubsystem.h`, `Private/Gadget/BoarGadgetSubsystem.cpp`
- `Public/UI/BoarUIFlowComponent.h`, `Private/UI/BoarUIFlowComponent.cpp`
- `Private/Core/BoarCaptureResolution.h/.cpp`
- `Private/Core/BoarGadgetLoadoutInitializer.h/.cpp`
- `Private/Tests/GadgetProgressTests.cpp`, `Private/Tests/CaptureResolutionTests.cpp`
- `RefactorAudit.md`, `RefactorReport.md`, `Verification/`の検証スクリプト・BP対象一覧・ログ除外設定。

新しいUClassはPresenter/Subsystem/UIFlowの3つ。Captureと初期化調整は状態を持たないprivate helperで、Interfaceは追加していない。

### 変更

- `Private/UI/BoarLoadoutWidget.cpp`, `Public/UI/BoarLoadoutWidget.h`
- `Private/Core/BoarGameInstance.cpp/.h`
- `Private/Core/BoarGameMode.cpp`
- `Private/Player/BoarPlayerController.cpp`, `Public/Player/BoarPlayerController.h`
- `Private/Component/GadgetComponent.cpp`, `Public/Component/GadgetComponent.h`

### 削除

なし。

Editorで既に未保存だった`DA_TestStageConfig`はユーザーの許可に従い保存した。これはリファクタリングによるAsset編集とは別の既存変更。

## 4. SOLID改善点

- **S**: Widgetは表示とFocus、Presenterは操作判断、SubsystemはGadget規則、GIは保存公開、UIFlowはメニュー管理に責務を分けた。GMからCage選択/収納処理を抽出した。
- **O**: Gadgetの操作規則はSubsystem、表示データの組立てはPresenter、メニュー生命周期はUIFlowを中心に変更できる。ただし新しい操作/メニュー追加が完全に無修正で済む設計ではない。
- **L**: `AGadgetBase`/`NetGadget`のUse・装備契約は変更していない。実測上、新Interfaceで解決すべき置換違反は見つからなかったため、継承構造は維持した。
- **I**: Loadout Viewへ渡すのは`FBoarLoadoutEntryViewData`と`FBoarLoadoutActionResult`。ViewはGadget定義やSaveDataを読む必要がなくなった。形式的なInterface追加はしていない。
- **D**: `GadgetComponent.InitializeResolvedLoadout`で初期Loadoutと一時装備方針を外側から渡す。ComponentからGI/GM/Saveへの直接依存を除去した。具象Subsystemへの依存と初期化互換入口は残るため、完全な抽象化ではない。

## 5. MVP

- **View**: `BoarLoadoutWidget`。既存BP Binding、Entry生成、表示、Focus、操作通知を担当。
- **Presenter**: `BoarLoadoutPresenter`。選択Slot/LabMode、候補順序、操作結果/文言、Model通知購読を担当。画面を閉じる際に購読を解除する。
- **Model**: `BoarGadgetSubsystem`（進行規則）と`GadgetComponent`（装備状態）。永続データと保存公開は既存GI/SaveGameが担当。

Lobby/図鑑/ArchiveまでMVPを一括展開していない。UIFlowはPresenterではなく、画面間の遷移を管理する。

## 6. 互換性維持

- **SaveData**: `BoarSaveGame`と`StageRunData`/`StageConfig`のソース契約は比較元と一致。保存候補複製→保存成功→公開の順序を維持。追加テストで保存失敗時の非公開、試用時の保存データ不変、Loadout ID復元/不明ID時の既定装備を確認した。
- **Blueprint**: 変更対象Headerの既存UFUNCTION署名/metadataと非Transient UPROPERTYをスクリプト比較。既存契約を保持し、41個の対象BPを再コンパイルした。
- **Gameplay**: Captureの健全Cage優先、距離/同距離順序、壊れたCageへのfallbackを自動テスト。既存Stage終了優先順位/入力Lock等のテストを継続した。全プレイ操作の実機回帰確認は未完了。
- **DataAsset**: Asset名/パス/型、GIの編集可能Catalog、ControllerのWidgetClass設定を保持。上記ユーザーの未保存変更を除いて、今回の設計変更にAsset移行は不要。

## 7. Compile / Test結果

| Phase | C++ Development Editorビルド | REBoarGetch.Spec | 対象BP |
|---|---|---|---|
| 変更前 | 成功 | 5/5成功 | Asset/依存を監査 |
| 1 Loadout MVP | 成功 | 5/5成功 | 41個、compile error/warning/failed load = 0 |
| 2 Gadget Subsystem | 成功 | 6/6成功 | 同上 |
| 3 Capture抽出 | 成功 | 7/7成功 | 同上 |
| 4 UIFlow | 成功 | 7/7成功 | 同上 |
| 5 Gadget初期化 | 成功 | 7/7成功 | 同上 |

既存5件: ArchiveDisplays / RunProgressAndSerialization / SpawnDefinitions / StageEndPriority / StageInputLockOwnership。
追加2件: GadgetProgressAndLoadout / CaptureDestination。
GadgetテストはGUIDの独立保存Slotと保存失敗ガードを使用し、ユーザーの保存Slotへ書き込んでいない。

BP commandlet全体の終了コードは1。変更前からあるGameFeatureDataのAssetManager設定エラーが原因で、BP compiler集計は0 errors/0 warnings/0 failed loads・41個コンパイル。commandlet全体が成功したとは扱っていない。

検証は`Verification/VerifyPhase.ps1`、公開契約比較は`Verification/VerifyPublicApi.py`で再実行可能。ログは`Verification/Phase*-Tests.log` / `Phase*-Blueprint.log`に出力しGit対象外。

最終binaryを読み込んだEditorで`Verification/PieSmoke.py`を実行し、4 Level・34項目すべて成功した（`PieSmokeResults.json`: error=null, save_files_unchanged=true）。

- Stage1: Playing到達、初期装備、Pauseの時間停止/移動・Look Lock、Loadout開閉と親Pauseへの復帰、Settings開閉、退出確認のキャンセル、Resume後の時間/Lock復元。
- L_GadgetLab: 実Levelの端末からLoadout生成、試用Netの選択/解除、閉じた後の入力復元。
- L_GadgetTest_Net: Slot 0の試用装備、Slot 2への移動と元Slot解除、空Slotへの変更。
- L_Archive: 実Levelの端末から図鑑生成/終了と入力復元。
- 各PIEの進行データが開始時と同一であること、Saved/SaveGamesの全`.sav`のSHA-256が開始前後で同一であること。

端末の確認はPIE内の既存ActorのOverlap UFUNCTIONを反射呼び出ししたもので、キーボード入力による歩行/Interact確認ではない。Widgetも公開API/既存UFUNCTION経由で操作し、内部Presenter参照の直接検査や画面の目視確認はしていない。最初の検証スクリプトでPython名/保護プロパティへのアクセスエラーを修正した後、全項目を再実行した。製品コードの変更は不要だった。

## 8. 未確認

- キーボード/ゲームパッド実操作のFocus、移動/カメラ/Jump/Dash/Net使用、実Actorの捕獲演出からClear/GameOver/Result/Retryまでの連続プレイ。
- 全Level/全AssetのCook・パッケージ版、別プラットフォーム、過去の各バージョンの実Saveファイル。
- Blueprintのコンパイルは上記41対象に限定。全プロジェクトAssetを対象とする検証ではない。

## 9. 残っている設計Debt

- Lobby/Encyclopedia/Archive Viewに残る進行データ参照。必要な変更機会に個別Presenter化を検討する。
- GIのStage/Archive永続化規則と互換Facade。今回はSave境界の移動を避けた。
- UIFlowとControllerは既存BP設定・入力処理を共有する。friend依存は残る。
- Stage初期化Coordinatorの具象GM/Facility依存、Actor停止の具象型分岐。現状より複雑なInterface群は導入しなかった。
- 既存GameFeatureData/AssetManager設定エラーは今回の設計変更の範囲外。
