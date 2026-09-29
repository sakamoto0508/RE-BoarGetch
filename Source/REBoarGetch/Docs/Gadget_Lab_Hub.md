# Gadget Lab Hub — 2026-09-27

## 2026-09-28 実物展示の更新

- 最新アート仕様の「アウトゲーム空間の展示物・装飾」に合わせ、Primitive製の仮アミを含む旧結合Mesh Actorを非表示のまま保存。削除・改名・元Assetの上書きは行わない。
- `BP_Display_Net`（SkeletalMeshActor派生）を中央Stationと壁面Collectionに配置。本編BP_NetGadgetと同じ `/Game/InportAssets/mushitoriami/mushitoriami` および同フォルダの `マテリアル` を参照。外部原本は編集していない。
- ComponentはSkeletalMeshComponentのみ。GadgetBaseを継承せず、使用・装備・Hit・Cooldown・Save処理なし。Collision、Overlap、Physics、Navigation影響、Actor/Component Tickを無効化。
- AnimationModeは本編と同じAnimationBlueprint、AnimClassなしの参照ポーズ。SingleNodeへ変更すると当該Meshの表示姿勢が崩れるため使用しない。
- `SM_GadgetLabDisplay_*` 7素材別Meshを追加。太い支持材、関節、左右操作パネル、中央展示台、壁面の展示フレーム、床区画を構成。実物アミの薄い色が読み取れるようNavy背板を追加。
- 既存Portal 3 Actorの座標・Action、Main LobbyへのReturn参照を確認。Unlock／Test選択／Save／Gadget使用のC++変更なし。
- Blueprint Compile、Save、Level再読込後の参照とNoCollision確認済み。中央・壁面の実物アミはEditor画像で表示確認済み。Runtime未実施。

## 実装範囲

ユーザー合意: HubとアミTestを先行。現在CatalogにはNetのみ。剣・Speed Boots等の使用処理、専用Level、解放枚数の具体設定は後続。架空のCatalogデータは追加していない。

## Levelと導線

- `/Game/Level/L_GadgetLab`: 中央Loadout & Unlock Station、壁面Collection、単一Test Portal、既存Main Lobby Return。
- `/Game/Level/L_GadgetTest_Net`: アミを一時装備する小規模Test室。2体の捕獲ダミーは捕獲後2秒で復帰。ReturnはL_GadgetLab。
- 旧Lab内Test Zoneと旧結合装飾31 Actorは削除せず非表示・Collision無効。新規設備MeshはNoCollision。
- 元のMain Lobby・Archive・Stage Levelは今回の変更対象外。

## Editor設定

GadgetDataAsset:

- `bInitiallyUnlocked`: 初期解放。DA_Gadget(Net)はtrue。将来の剣も実装時trueにする。
- `RequiredSpecialCoinCount`: 累計条件。具体値はEditorで調整。条件達成だけで自動解放しない。
- `TestLevel`: 対象専用WorldへのSoft参照。NetはL_GadgetTest_Net。
- 旧`RequiredClearedStageId`は互換用に残すが、新規解放判定には使わない。

GameInstanceのGadgetCatalog順がUI固定順。`UnlockedGadgetIds`を既存Saveに追加保存し、Coin ID集合は変更しない。保存失敗時は解放状態を公開しない。既に保存された解放を取り消さない。

Test対象は`SelectedTestGadget`（GameInstance Transient）。装備Slotと独立し、Level往復で維持、アプリ再起動でリセット。解除可能。

HubTestTriggerのAction=GadgetTest。選択なしではTestVisualActorsを非表示・Trigger無効。解放済み選択で表示、TestLevel設定時のみTrigger有効。CollectionLabelsはCatalog順のTextRender参照（現在Netの1枠）。新しいGadgetの展示を追加する場合もこの順序を合わせる。

各専用Test GameModeはBoarFacilityGameModeを継承し、`bGadgetTest=true`、`TestGadgetClass`を対象Classに設定。試用装備をSlot1へ一時設定し、通常の保存装備を上書きしない。各Return PortalはL_GadgetLabを参照。

## 変更クラス

GadgetDataAsset、BoarGameInstance、BoarFacilityGameMode、GadgetComponent、BoarLoadoutWidget、BoarLoadoutEntry、HubPortal、BoarPlayerController。NetPracticeBoarを追加。既存Net使用・Cooldown・Stage捕獲処理は変更していない。

## 検証

Development Editorビルド成功。対象Blueprint Compile、Level/Asset Save、参照のEditor再取得で確認。PIE/Runtimeはユーザー指定で未実施。

ユーザー確認: Lab操作切替とゲームパッドFocus、装備していないNetのTest選択、選択解除でPortal非表示、Net捕獲と復帰、LabへのReturn、通常Loadout保持、LabからMain LobbyへのReturn。Manual Unlockの成功/不足/再起動保持は未解放Gadget追加後に実行確認が必要。
