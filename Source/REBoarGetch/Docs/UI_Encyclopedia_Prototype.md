# イノシシ図鑑UI Prototype

## 範囲

- Notion「イノシシ図鑑仕様書」`3dfb6c887525814db14af7f661b90c3c`を確認。
- ユーザー指定：UIと閲覧入口を先行し、個体データは未登録で残す。固定個体IDへの移行、Spawn・3D世界の変更は行わない。
- 調査時点のStage01は旧BoarSpawnEntriesが5体、BoarSpawnDefinitionsは空。写真・名前・説明・特徴も未登録。

## 実装

- BoarEncyclopediaWidgetがStageCatalog内のStageConfig.BoarSpawnDefinitionsから個体一覧を構成。Actor数、出現順、実行時GUIDは使用しない。
- 捕獲判定はBoarGameInstance.GetProgress().CapturedBoarUniqueIdsのみ。図鑑からSaveDataを書き換えない。
- 個体IDなし・重複ID・Classなしの定義は対象外。全体進捗は有効な登録個体のみを分母とする。未登録時は0/0完了扱いせず未登録メッセージ。
- 未捕獲はStage名以外の個体情報を???で隠し、写真を読み込まない。捕獲済みで素材未登録なら「写真未登録」、文面未登録なら「未登録」。
- 種類は既存BoarClassのGetBoarArchetypeから取得。捕獲済みIDから種類別進捗を集計し、未捕獲種名は非表示。
- 固定個体が所属するStageを初捕獲Stageとして表示。別Stageへ同じIDを再配置する運用は対象外（重複IDを除外）。
- FBoarSpawnDefinitionへ任意の図鑑名・説明・特徴・写真フィールドを追加。既存フィールドは維持し、実データの推測登録はしない。
- 左一覧の行は既存BoarLoadoutEntry／WBP_LoadoutEntryを再利用。右に写真と個体情報。全体進捗・種類進捗を図鑑内へ表示。
- Lobbyのステージ選択画面と共通Settingsへ図鑑ボタンを追加・接続。閉じると親画面と図鑑ボタンFocusへ戻す処理、Pause状態を維持する処理を追加。
- 閲覧中は移動・視点入力とゲーム操作を遮断。戻る入力は既存IA_UI_Backを使用。

## 確認

- Development Editor C++ビルド成功（2026-09-23）。Unreal MCP再接続後に以下を実施。
- WBP_BoarEncyclopediaを作成（BoarEncyclopediaWidget派生、SafeZone、1120×800、19要素）。既存LoadoutパネルMaterial、左ScrollBox個体一覧、右写真・詳細ScrollBox、進捗、「戻る」。EntryClassは既存WBP_LoadoutEntry、StageCatalogはDA_TestStageConfig。
- WBP_SettingsへButton_Encyclopediaを追加（21要素）、EncyclopediaButtonNameを設定。640×660へ拡張し既存MI_UI_HoloPause_Loadout_Prototypeを再利用。
- WBP_LobbyStageSelectへButton_Encyclopediaを追加（22要素）、EncyclopediaButtonNameを設定。StagePanelは1000×760、既存Start/Cancelは維持、図鑑はY=582。専用MI_UI_HoloStage_Encyclopedia_Prototypeを作成。
- BP_BoarPlayerController／BP_PC_Lobby／BP_PC_TitleのEncyclopediaWidgetClassを接続。
- 上記6 Blueprintを警告もエラーとしてCompile成功。Materialを含む対象7 AssetのSave成功、再取得でWidget構造・Class参照・入口Bindingを確認、Dirty=false。
- StageConfig再取得でBoarSpawnDefinitions=[]、旧BoarSpawnEntriesの5体設定を維持していることを確認。
- PIE・UE実行テストはユーザー指定により実施しない。
- 実個体での捕獲済み表示・再起動復元の確認には、後工程で固定個体IDと図鑑データの登録が必要。
