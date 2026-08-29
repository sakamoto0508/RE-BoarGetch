# RE:BoarGetch Codex作業ログ

最終更新: 2026-08-22

## 目的

別のCodexチャットや別日に作業を再開するときの引き継ぎ資料です。新しい作業を始める前に、本書とNotionの最新仕様を確認してください。

仕様の正本はNotionです。本書は実装状況と直近の作業方針を素早く把握するための補助資料であり、仕様変更時はNotionを優先します。

## プロジェクト情報

- Unreal Engine 5.8
- C++作業場所: `C:\UnrealGames\REBoarGetch\Source\REBoarGetch`
- プロジェクト: `C:\UnrealGames\REBoarGetch\REBoarGetch.uproject`
- 主なテストMap: `Content/Level/Test.umap`
- プレイヤーAnimBP: `Content/BP/Player/ABP_Player.uasset`
- 説明、報告、コードコメントは日本語で行う

## 作業開始時に確認するNotion

- `RE:BoarGetch（仕様書）`
- `仕様書タスク（Private）`
- 作業対象に対応する個別仕様ページとタスク

ステージ関連で確認済みのページ:

- `ステージ`
- `ゲームループ仕様書`
- `ステージ：StageConfig DataAsset`
- `ステージ：指定捕獲数によるクリア判定`

## 実装済み・動作確認済み

### イノシシAI

- NormalイノシシをStateTreeで制御
- Patrol、檻の認識・追跡・攻撃が動作
- プレイヤー認識時のEscapeが動作
- 檻攻撃は予兆、突進、後退のフェーズで進行
- 種別ごとのAI、戦闘、知覚、スタミナ、移動設定を`UBoarDataAsset`で管理可能
- DataAsset未設定時はC++の種別既定値へフォールバック
- 檻破壊時に捕獲中のイノシシを解放
- 解放後はNavMesh上へ移動し、通常のStateTree AIへ復帰

### 捕獲と網ガジェット

- プレイヤーの網Montageを実装済み
- AnimNotifyから捕獲判定窓をON/OFF
- `BP_NetGadget`の`CaptureCollision`に重なった未捕獲イノシシを捕獲
- 判定窓開始時点ですでに範囲内にいる個体も対象
- 複数個体を一括捕獲可能
- 捕獲範囲はBlueprintの`CaptureCollision`で管理。旧`CaptureRadius`は削除済み
- 網使用中は移動不可、回転可能
- 被弾時にMontageを中断し、捕獲判定も終了
- C++共通終了処理`EndGadgetUse`を実装済み
- 旧`UseGadget`、`UseCurrentGadget`、`DefaultGadgetClass`は削除済み

### ガジェット装備

- ガジェットはプレイヤーSkeletal MeshのSocketへ装着
- 各ガジェットが`EquipSocketName`を所有し、網は`NetSocket`を使用
- 最大4つのガジェットスロットを管理
- 新装備の生成・Socket接続失敗時は既存装備を維持

### 檻

- 捕獲個体を檻へ収容
- 配置専用の`CapturedBoarArea`内へ複数個体を分散配置
- Blueprint側の4つの`WallCollision`で壁を構成
- `CageMesh`、`WallCollision`、`CapturedBoarArea`はNavMeshへ影響しない設定
- 檻内部と外部のNavMesh接続を確認済み
- HP、破壊、一定時間後のRespawnが動作
- 破壊時はActorを破棄せず、非表示・Collision無効にして再利用

### Collision

- 旧Object Channelの`Boar`は削除済み
- イノシシは`Pawn` Channelを使用

### プレイヤー

- 移動、視点操作、ジャンプ、二段ジャンプ、ダッシュを実装
- HP、接触ダメージ、スタン、無敵時間、死亡通知を実装
- `EPlayerActionState`: `Idle / Walk / Run / Jump / DoubleJump / Fall / Stun / UseGadget / Capture`
- 現時点で完成している主要アニメーションは網の捕獲Montage

### 指定捕獲数によるステージクリア

- `UStageConfig : UPrimaryDataAsset`と`TargetCaptureCount`を実装
- `ABoarGameMode`へ`StageConfig`設定欄を追加
- 捕獲成功後に累計数を更新してクリア条件を評価
- 到達時にBlueprintイベント`OnStageCleared`を一度だけ通知
- `bStageClearRequested`で多重通知を防止
- `StageConfig`未設定または`TargetCaptureCount <= 0`では捕獲数クリア判定を無効化
- ユーザーのPIEテストで、到達前・到達時・多重通知防止に問題なし

## 確認済みテスト

- 捕獲 → 檻収容 → 檻破壊 → 解放 → 通常AI復帰
- 檻Respawn
- 網の判定窓、範囲内捕獲、複数捕獲
- 網使用中の移動禁止と回転
- 被弾時のMontage中断と判定終了
- 指定捕獲数によるクリア判定と一度だけの通知

## コンパイル状況

- `UStageConfig`と`OnStageCleared`追加後のUnrealHeaderTool処理は成功
- コメント整備後もUnrealHeaderTool処理は成功（37生成ファイル）
- 通常C++ビルドはコードエラーではなく、EditorのLive Coding有効中だったためUE側に拒否された履歴あり
- `Ctrl + Alt + F11`でLive Codingするか、Editor終了後に通常ビルドして最終確認する

## コードコメント整備

- 既存コメントを削除せず、日本語コメントを追加済み
- `.h`の型、列挙値、関数、変数へサマリーを補足
- StateTree、攻撃フェーズ、ガジェット装備、スロット、プレイヤー状態、クリア判定の意図を追えるよう整理
- Epicテンプレート由来の`Variant_*`は対象外

## 現在の最優先作業

クリア後のゲーム進行より先に、プレイヤーの基本アニメーションを整備する方針です。

必要なアニメーション:

- Idle
- Walk
- Run
- Jump Start
- Fall Loop
- Land
- Double Jump
- 既存の網Montageとの遷移調整

目標はPS1・PS2時代のようなリアルすぎない動きです。短いループ、大きなポーズ、少ない中間動作、短いBlendを優先し、過剰な足IKやMotion Matchingは後回しにします。

### 推奨AnimBP構成

```text
Locomotion State Machine
        ↓
DefaultSlot（既存の網Montage）
        ↓
Output Pose
```

```text
Entry → Grounded（Idle / Walk / Runの1D Blend Space）
      → JumpStart → FallLoop → Land → Grounded
                     ↓
                 DoubleJump → FallLoop
```

AnimBPで更新する値:

- `Speed`: VelocityのXY長
- `bIsInAir`: CharacterMovementの`Is Falling`
- `VerticalSpeed`: Velocity.Z
- `PlayerActionState`: `GetPlayerActionState()`
- `bIsMoving`: `Speed > 5.0`

C++移動速度は`WalkSpeed = 500`、`DashSpeed = 900`。1D Blend SpaceはSpeed 0=Idle、500=Walk、900=Runを初期値とし、State間Blendは`0.05～0.12秒`から調整します。

素材候補はMixamo、itch.io、Fab。FBX/UE形式、Humanoid Skeleton、In-place、リターゲット可否、商用条件を確認します。

## アニメーション後の予定

1. 基本Locomotionを完成
2. 二段ジャンプと網Montageの遷移確認
3. C++コンパイルとPIE回帰テスト
4. ステージ終了状態（`Playing / Cleared / GameOver`）の共通化
5. クリアとゲームオーバーの排他制御
6. クリア時の操作停止
7. リザルトへ捕獲数・クリアタイムを渡す
8. リザルト終了後にロビーへ戻す

ステージ終了状態の共通化は案のみで、まだ未実装です。

## 主な関連コード

- `Public/Player/BoarPlayerCharacter.h`
- `Private/Player/BoarPlayerCharacter.cpp`
- `Public/Player/BoarPlayerController.h`
- `Private/Player/BoarPlayerController.cpp`
- `Public/Animation/AnimNotify_NetCaptureWindow.h`
- `Private/Animation/AnimNotify_NetCaptureWindow.cpp`
- `Public/Gadget/GadgetBase.h`
- `Public/Gadget/NetGadget.h`
- `Private/Gadget/NetGadget.cpp`
- `Public/Component/GadgetComponent.h`
- `Private/Component/GadgetComponent.cpp`
- `Public/Boar/BoarBase.h`
- `Private/Boar/BoarBase.cpp`
- `Public/Boar/BoarDataAsset.h`
- `Public/Boar/StateTree/*`
- `Private/Boar/StateTree/*`
- `Public/Cage/Cage.h`
- `Private/Cage/Cage.cpp`
- `Public/Stage/StageConfig.h`
- `Public/Core/BoarGameMode.h`
- `Private/Core/BoarGameMode.cpp`

## 新しいチャットでの開始手順

1. このファイルを読む
2. Notionの仕様書と対象タスクを読む
3. `git status`と対象差分を確認
4. Editor、Live Coding、PIEの状態を確認
5. C++とBlueprintに重複実装がないか確認
6. 既存のユーザー変更と無関係な変更を保持
7. 実装前に調査結果と問題点を日本語で報告
8. 実装後にC++コンパイルと可能なPIE確認
9. 完了後、この作業ログへ実装・検証・次工程を追記

## 注意事項

- Content内のBlueprint、AnimBP、DataAsset、Mapにはユーザー作業が含まれるため、上書き・削除しない
- 未コミット変更が存在する前提で、対象外ファイルへ触れない
- Git LFS一時ディレクトリの権限により、Sandbox内の`git status`が失敗する場合がある
- `OnStageCleared`は通知のみ。操作停止、リザルト、Map遷移はまだ共通実装されていない
- AnimBPはバイナリAssetなので、編集前に対象AssetとSkeletonを必ず確認する
- コードコメントは日本語とし、既存コメントを無断で削除しない

