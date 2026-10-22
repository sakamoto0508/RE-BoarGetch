# 起動と初期化の流れ

ステージ開始の進行役は `ABoarGameMode::StartPlay()`。生成処理そのものは各担当に残し、順序と成功判定をGameModeで管理する。

## ステージ

1. `UBoarGameInstance::Init()` が永続進行を読み込む。
2. UEがレベル配置Actor・Controller・デフォルトPawnを構築する。
3. `ABoarGameMode::PrepareStage()` が設定と進行データの使用可否を確認する。
4. ステージ操作を停止し、設定されたボア・コインを生成する。個体IDは生成完了前に設定する。
5. 檻、ボアの種類設定、プレイヤーHP・装備を初期化する。装備方針と進行への参照は `FBoarGadgetLoadoutContext` で渡す。
6. 初期装備の生成成功と、目標に必要なボア数・檻・プレイヤーの存在を確認する。
7. `Super::StartPlay()` が通常のActor／ComponentのBeginPlayを実行する。イベント登録、HUD生成、ローカルな見た目の準備は従来の所有者が担当する。
8. 追加された演出Actorなども含めて停止状態に揃え、開始演出を要求する。
9. `CompleteStageIntroduction()` で初めて `Playing` に進み、入力・移動・AIを開始する。

初期化に失敗した場合もUEのBeginPlayは実行するが、ステージの準備完了フラグは立てない。エラー理由を公開し、標準の再読込／ロビー復帰画面を表示する。開始演出の完了通知で失敗を飛び越えることはできない。

## 施設・ロビー・追加生成

- `ABoarFacilityGameMode::StartPlay()` はBeginPlay前にプレイヤーを準備する。Testでは専用ガジェットと一時装備方針を渡す。
- 既存の独自GameModeで動くロビー、およびプレイ中に追加生成されたプレイヤーは、PlayerのBeginPlayから同じ `InitializeForPlay(Context)` を呼ぶ。HP／装備ComponentはGameModeの種類やPreparing状態を判定しない。
- 単独で置かれたHP Component・檻・ボアはBeginPlayから同じ初期化を呼べる。初期化済みならHPや装備を再設定しない。
- 初期装備の生成に失敗したComponentは未初期化のままなので、修正した設定で初期化を再試行できる。
- ActorやComponentの構築・登録と、ゲーム開始用の初期化は別の段階として扱う。コンストラクタからステージ進行を開始しない。

## 拡張時の入口

- 新しい必須Actorの生成は `PrepareStage()` の生成段階に追加し、失敗をboolで返す。
- Actor間の依存解決はGameMode側で行い、初期化引数として渡す。
- 新しい準備処理は `InitializeStageActors()` に追加する。初期化済み判定は成功後に確定する。
- 非同期の準備を追加する場合は、その完了前に `bStageInitializationComplete` を立てない。
- Blueprintの既存イベント・編集項目と、セーブデータの形式は維持する。

## 検証

`REBoarGetch.Spec.StageInitialization` はBeginPlay前の準備、通常の生成・開始演出待ち、再実行、設定不備・生成依存の欠落を確認する。
`REBoarGetch.Spec.LoadoutInitializationFailure` は装備失敗時に準備完了とせず、修正後に再試行できることを確認する。検証は固有名のセーブスロットを使用する。

### 実行結果（2026-10-21）

- UE 5.8.3 / Win64 Development EditorのC++ビルド成功。
- `REBoarGetch.Spec` 全10件成功（警告付き成功3件を含む）。結果は `Verification/InitializationAutomation/index.json`。
- 既存のPlayer BlueprintとNet Blueprintを使い、BeginPlay前の装備生成・ソケット接続に成功。
- 関連Blueprint 10件はメモリ上のコンパイルですべて `BS_UP_TO_DATE`。`Verification/InitializationCompatibility.json` の個別結果を参照。
- Blueprint検証前後のSaveGames内のファイルハッシュは一致。保存形式とStageConfigのソースに変更なし。
- Blueprint検証コマンドレット自体は、プロジェクト起動時の既存 `GameFeatureData` / AssetManager設定エラー2件により終了コード1。検証スクリプト内のエラーは0件。設定ファイルは変更していない。
- NullRHIで検証したため、PIEでの見た目・手動操作・エラー画面の表示確認は未実施。
