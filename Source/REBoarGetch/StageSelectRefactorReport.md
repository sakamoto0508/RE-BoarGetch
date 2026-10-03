# Stage Select MVP リファクタリング (2026-10-03)

## 作業前Audit / 設計判断

- Repository: `https://github.com/sakamoto0508/RE-BoarGetch.git`、branch `main`、HEAD/remote mainとも `8a7b2365f4e01dbaf35d9e07fc9ec5a45e2447cd`。`git ls-remote`で確認。既存変更は`Config/DefaultEngine.ini`のみ。
- 適用するAGENTS.mdは見つからなかった。UE 5.8、開始時に実行中のEditorなし。
- Lobby Widget、Loadout Presenter、Gadget Subsystem、UIFlow、GI、SaveGame、StageConfig、PreviewActor、StageEntranceをソースから確認。
- 呼び出し経路: Controllerの共通Interact → StageEntrance.Interact → Widget生成/AddToViewport → ShowStageCatalog。旧Delegateによる開始要求はStageEntranceでCatalog/Unlock/Levelを再検証し、LastAttempted保存成功後だけOpenLevel。Cancel/Overlap退出は共通Close経路。
- WidgetがCatalog重複除外、LastAttempted/Fallback/初期選択、Unlock/Clear判定、Coin/個体ID進捗、Start判定、選択IndexとCarousel遷移先を管理している。
- 一覧はUnlock済みStageだけ選択して循環する。Carouselは端で停止し、Locked Stageも閲覧用の中央位置へ移動できる。Locked表示では開始できない。この差を仕様として維持する。
- PreviewActorは静的ミニチュア専用で、Gameplay Levelを読み込まない。Actor/RenderTarget/Material生成とアニメーションはWidgetへ残す。
- GIの既存通知はGadget解放等に限定され、Stage進行全体の通知はない。保存成功後のnative Delegateを一つ追加してPresenterへ通知する。保存失敗時の通知・公開は行わない。
- Presenterを一つ追加し、ViewDataはEntry/詳細/Carouselで共通のstruct一つを使う。選択・判定・表示データをPresenterへ移し、WidgetはIntent、描画、Focus、Scroll、Animationのみを担当する。Interface/Service/BasePresenterは追加しない。

実Asset監査では`WBP_LobbyStageSelect`はCarousel=true、時間=.18秒、Entry=`WBP_StageSelectEntry`、Preview Material=`M_UI_Diorama`。`BP_StageEntrance_Stage01`のCatalogは`DA_TestStageConfig`一件。StageId=Stage01、Level=Stage1、初期Unlock、Coin定義0件、個体定義5件だった。監査スクリプトはAssetを保存していない（commandlet終了コード1は既存GameFeatureData設定エラー）。

## 1. 変更前

`BoarLobbyWidget`はWidget生成/表示/Focus/Scroll/入力/Animation/Previewに加え、Catalog重複除外、初期選択、LastAttempted復元、Unlock/Clear判定、Coin/捕獲進捗、Start判定を担当していた。Saveの具体的なID集合も直接読んでいた。

## 2. 変更後

- **View / BoarLobbyWidget**: Widget Binding、Entry生成、描画、Focus、Scroll、キー入力通知、Visibility、Carousel補間、Preview Actor/Material/RenderTarget生成と破棄。
- **Presenter / BoarStageSelectPresenter**: Catalog、重複StageId除外、選択/遷移先Index、LastAttempted→Fallback→最初のUnlockedという初期選択、一覧/Carouselの移動規則、Unlock/Clear/Start判定、Stage内の固有ID集合による進捗、表示データ。
- **Model / GI・SaveGame・StageConfig**: 既存の永続進行・保存成功後公開・Stage解放規則・静的Stage定義。StageEntranceは開始の再検証、LastAttempted保存、Level遷移をそのまま担当。

## 3. 依存図

```text
StageEntrance (既存の開始/Cancel経路)
  └─ BoarLobbyWidget (View)
       └─ BoarStageSelectPresenter
            ├─ BoarGameInstance ─ BoarSaveGame
            └─ StageConfig

GI.SaveCandidate成功 → OnProgressChanged → Presenter → OnViewChanged → View更新
Hide/NativeDestruct → Presenter.Shutdown → Model Delegate解除

PresenterのPreview用ViewData → ViewがPreviewActor/RenderTarget/Materialを生成
```

PresenterはWidget、Focus、Actor生成、RenderTarget、Material、Animationを持たない。ViewDataは共通の`FBoarStageSelectViewData`一つで、Interface/Service/BasePresenterを追加していない。

## 4. 新規File

- `Public/UI/BoarStageSelectPresenter.h`
- `Private/UI/BoarStageSelectPresenter.cpp`
- `Private/Tests/StageSelectPresenterTests.cpp`
- `Verification/AuditStageSelect.py`
- `Verification/StageSelectPieSmoke.py`
- `StageSelectRefactorReport.md`

新しい製品UClassはPresenter一つ。PIE検証の一時Stage用Python classはテスト実行中だけ生成され、Assetを作成/保存しない。

## 5. 変更File

- `Public/UI/BoarLobbyWidget.h`, `Private/UI/BoarLobbyWidget.cpp`: Presenter接続とViewData描画。公開API、Binding名、編集設定、Delegateは保持。
- `Private/Core/BoarGameInstance.h/.cpp`: nativeの`OnProgressChanged`と保存成功後のBroadcastのみ追加。保存形式/条件/公開順序は保持。
- `Verification/VerifyPublicApi.py`: 比較元SHA指定とStage Select Header比較を追加。
- `Verification/.gitignore`: 分離したPIEのUserDir生成物を除外。

削除・Asset編集/改名なし。既存の`DefaultEngine.ini`変更は操作していない。指定されたLoadout/Gadget/UIFlow/Capture/初期化/GMは再リファクタしていない。

## 6. Widgetから削除できたModel依存

`BoarGameInstance`/`BoarSaveGame`/`StageConfig`のincludeと具体的な解析を削除。`GetProgress`、`ClearedStageIds`、`SpecialCoinIds`、`CapturedBoarUniqueIds`、Unlock判定、進捗計算、Catalog/選択Index保持はPresenterへ移した。

Viewには汎用`GetGameInstance()`をPresenter初期化へ渡す処理と、既存開始Delegateの`UStageConfig*`契約だけ残る。PreviewにもStageConfigを直接渡さず、ViewDataのID/Soft Class/Thumbnailを使う。

## 7. SOLID改善

- **S**: Viewは表示と操作表現、PresenterはStage Selectの判断、Modelは既存進行規則・保存を担当。Carousel時間/Scale/Opacity/TranslationはViewに残した。
- **I**: ViewはPresenterのCatalog数、ViewData、選択/Step/Start APIだけを利用し、巨大なGI/SaveGameの詳細を読まなくなった。形式的なInterfaceは追加していない。
- **D**: Viewから具体的な永続ID集合への直接依存を除去した。Presenterの具象GI/StageConfig依存は現在の規模に合わせて残した。全面的な抽象化ではない。

## 8. Compile / Test結果

- UE 5.8 / `REBoarGetchEditor Win64 Development`ビルド成功（UHT、C++、Link）。
- `REBoarGetch.Spec` **8/8成功**、既存7件を保持。`Verification/Phase6-Tests.log`。
- 追加`StageSelectPresenter`テストで、重複/None/無効Stage除外、順序、Locked選択拒否、Unlocked選択、LastAttempted/Fallback/空Catalog、Clear表示、重複/None/他Stage IDを除いたCoin/捕獲進捗、Level未登録時Start拒否を確認。
- 一覧の循環/Lockedスキップ、Carouselの端停止/Locked閲覧/移動中Start拒否/連続要求、Preview可視範囲も確認。
- ViewData参照/選択時のSaveGameメモリ直列化バイト一致、保存成功後公開通知、失敗時の非公開/通知なし、Shutdown購読解除も確認。C++ fixtureはGUID Slotを使い、終了時に削除。
- 最終`git diff --check`成功。公開契約比較とSaveGame/StageConfig/StageRunDataソース一致も確認。

## 9. Blueprint確認

対象41個を再コンパイルし、compiler集計は **0 errors / 0 warnings / 0 failed loads**。Stage Select関連の対象には以下を含む。

- `/Game/BP/Widget/WBP_LobbyStageSelect`
- `/Game/BP/Widget/WBP_StageSelectEntry`
- `/Game/BP/Lobby/BP_StageEntrance_Stage01`
- `/Game/BP/Lobby/BP_PC_Lobby`, `BP_GM_Lobby`
- その他の既存Widget/Player/施設等（`Verification/BlueprintAllowList.txt`）。

commandlet全体の終了コードは1。変更前のAsset監査でも発生したGameFeatureData/AssetManager設定エラーが原因。commandlet全体の成功とは扱っていない。ログは`Verification/Phase6-Blueprint.log`。

既存UFUNCTION署名/metadata、非Transient UPROPERTY、Assignable Delegate、Binding名をHEAD比較で維持確認。CarouselのMaterial、アニメーション定数、入力キー定義、Preview生成パラメータは維持している。

## 10. PIE確認

最終再実行は **31項目成功 / error=null**。実Lobby Level・Controller・StageEntrance・Stage Select BPを使用した。

- Stage Select生成、Stage名/状態/進捗表示、Start有効状態、1件CatalogのPrevious/Next無効、初期Start Focus。
- CancelによるWidget除去と移動/Look入力Lock解除。
- PIE内だけの一時CatalogでLockedカード、表示伏せ/Start不可、Entry intent選択/拒否、Next/Previous、連続Next、重複ID除外、Coin/個体進捗テキスト、Carousel補間後のTransform、Focus復帰。
- 実Preview Actor生成/可視範囲への復帰。
- 既存StageEntrance経由のStart→Stage1 Playing、LastAttempted記録。
- `-UserDir`で分離したSavedディレクトリをPIE開始前/Start直前に検証。最終再実行前後の通常SaveファイルのSHA-256は一致。

検証は既存Overlap/UFUNCTIONを反射呼び出ししている。実物理Keyboard/Gamepad入力ではない。背景描画を検証中だけ有効にし、設定を戻した。

**検証中の保存更新:** 分離方式を修正する前のStart検証で、CDOの保存Slot設定がPIE実体へ反映されず、通常の`Saved/SaveGames/REBoarGetch_Progress.sav`にLastAttemptedStage=Stage01が保存された。通常のStart保存経路による更新で、Save形式は変更していない。事前のファイルコピーを保持していなかったため元のバイト列へ復元していない。最終再実行のハッシュ一致は、この更新後から再実行終了までの確認である。

Python名/WidgetTree探索/EditDefaultsOnly制約/アニメーション待機/背景描画の検証スクリプト上の問題も修正して全項目を再実行した。これらのために製品コードを追加修正する必要はなかった。

## 11. 未確認

- 物理Keyboard/Gamepadの入力配送、Stick/D-Pad長押し、各デバイスのFocus見た目。
- ピクセル比較によるUI外観一致、全解像度/全言語、全実Stage Asset/全Level、Cook/パッケージ版。
- 最終PIEは分離Saveの初期進行と一時Catalogを使った。任意の過去Saveバージョン・多数Stageの組合せを網羅していない。
- 捕獲からClear/Result/Retryまでの連続Gameplay回帰確認は今回の対象外。既存の自動テストは成功。

## 12. 残っている設計Debt / 次点Audit

Stage SelectのCompile/Test/BP/PIE確認後に、EncyclopediaとArchiveSelectionをソース監査した。実装変更はしていない。

- **Encyclopedia**: `BuildEntries`がSaveの捕獲IDとStageCatalogを直接読み、重複個体IDを除外し、種類別/全体進捗を計算。`SelectEntry`も捕獲状態、伏せ表示、個体詳細を解析する。独立した`BoarEncyclopediaPresenter`候補として価値が高い。写真ロード・Widget生成・FocusはViewに残す構成が適する。
- **ArchiveSelection**: 捕獲履歴の逆順/旧Saveの安定したfallback、個体定義探索、3枠の候補/選択と保存結果がViewに混在する。展示編集が増える段階で専用Presenter化する価値はある。Encyclopediaと無理に共通Base化する必要はない。
- **GI互換Facade**: 既存BP設定/APIを維持する目的で現在は妥当。将来さらにGadget機能を追加する際に移行範囲を検討する。今回Stage専用Subsystem/Repositoryは不要。
- **UIFlowとControllerのfriend依存**: BPのWidgetClass/Input設定を維持するため残る。メニュー増加やテスト困難が具体化した時に設定アクセス境界を整理する価値があるが、今回は変更していない。
- Stage Select PresenterはGI/StageConfigに具象依存する。現在は単一Presenterで読みやすく、Interface群を増やす利益は薄い。

## 再実行

```powershell
& Verification/VerifyPhase.ps1 -Phase 6 -ExpectedTests 8
python Verification/VerifyPublicApi.py --baseline 8a7b2365f4e01dbaf35d9e07fc9ec5a45e2447cd --stage-select
```

PIE scriptは通常Savedを保護するため、必ず次の引数でEditor起動に渡す。分離パスが異なる場合はStart前に検証が停止する。

```text
REBoarGetch.uproject /Game/Level/L_Lobby
-UserDir=C:/UnrealGames/REBoarGetch/Source/REBoarGetch/Verification/StageSelectPieUser
-ExecutePythonScript=C:/UnrealGames/REBoarGetch/Source/REBoarGetch/Verification/StageSelectPieSmoke.py
```
