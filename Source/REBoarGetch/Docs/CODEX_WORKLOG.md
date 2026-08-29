# RE:BoarGetch Codex Worklog

最終更新: 2026-08-28

## このファイルの目的

別のCodexタスクや新しいチャットで作業を再開するときの引き継ぎ資料です。
最初にこのファイルを読み、完了済みの作業を重複して実装せず、未完了項目と次の作業候補を確認してください。

このファイルには、次の2種類の情報を区別して記録しています。

- **コード確認済み**: `Source/REBoarGetch` のC++で存在を確認したもの
- **ユーザーPIE確認済み**: Unreal Editor上のBlueprintやアセットを含め、ユーザーが実動作を確認したもの

Blueprint、Animation Blueprint、MontageなどのContentアセットは、このSourceディレクトリだけでは内容を完全に検査できません。ユーザーPIE確認済みの記録を現状の根拠として扱ってください。

---

## 現在地（要約）

プレイヤーの基本Locomotionとアミ捕獲の主要ループは完成しています。

- Idle / Walk / Run
- Jump Start / Fall Loop / Land
- Double Jump
- アミ振りFullBody Montage
- AnimNotifyによる捕獲判定窓
- 使用中の入力ロックとMontage終了後の復帰
- Cooldown、空振り、連打耐性
- 捕獲数によるステージクリア判定のC++側

リザルト画面はBlueprint接続とRetry後の操作復帰までPIE確認済みです。

`WBP_PlayerHUD`のHP／捕獲数表示、値更新、Result表示時の非表示、Retry後の再表示までPIE確認済みです。

**次に着手する推奨タスクは、NotionのP1タスク「UI：現在ガジェットと4スロット表示」です。**

```text
目標捕獲数に到達
  -> ABoarGameMode::OnStageCleared
  -> プレイヤー操作停止
  -> WBP_Resultを表示
  -> UI入力へ切り替え
  -> リトライ / タイトル遷移
```

スタン用アニメーションはユーザーの希望により後回しです。

---

## 1. プレイヤーLocomotion

### 完了・確認済み

**ユーザーPIE確認済み**

- `ABP_Player`にLocomotion State Machineを作成
- Grounded内は`Speed`だけを使う1D Blend Space
- プレイヤーは移動方向を向き、前方向のアニメーションだけを使うため`Direction`は不使用
- Idle / Walk / Runが速度に応じてブレンド
- JumpStart / FallLoop / Landが遷移
- 崖から落ちた場合はGroundedからFallLoopへ直接遷移
- DoubleJumpアニメーションが二段目のジャンプ入力時に再生
- 三回目のジャンプ入力では実ジャンプもDoubleJumpアニメーションも発生しない

### ABPで使用している情報

```text
PlayerCharacter
Speed
VerticalVelocity
bIsInAir
ActionState
PreviousActionState
bDoubleJumpRequested
```

`bDoubleJumpRequested`は次の式で1フレームだけ立つパルスとして扱います。

```text
(ActionState == DoubleJump)
AND
(PreviousActionState != DoubleJump)
```

その直後に、毎フレーム必ず次を実行します。

```text
PreviousActionState = ActionState
```

### State Machine構成

```text
Entry
  -> Grounded

Grounded
  -> JumpStart
  -> FallLoop

JumpStart
  -> DoubleJump
  -> FallLoop
  -> Land

FallLoop
  -> DoubleJump
  -> Land

DoubleJump
  -> FallLoop
  -> Land

Land
  -> Grounded
  -> JumpStart
```

重要な遷移条件:

```text
Grounded -> JumpStart
bIsInAir && VerticalVelocity > 0

Grounded -> FallLoop
bIsInAir && VerticalVelocity <= 0

JumpStart / FallLoop -> DoubleJump
bDoubleJumpRequested

JumpStart / DoubleJump -> FallLoop
bIsInAir && VerticalVelocity <= 0

FallLoop / DoubleJump -> Land
!bIsInAir

Land -> Grounded
Landアニメーション終了
```

### DoubleJumpで修正した不具合

以前は二段目ではなく三回目のジャンプ入力時にDoubleJump状態が通知されていました。
原因は、`Jump()`直後に`JumpCurrentCount >= 2`を確認していたことです。`JumpCurrentCount`の更新は入力呼び出しと同時とは限らないため、判定が1入力遅れていました。

現在の[BoarPlayerCharacter.cpp](../Private/Player/BoarPlayerCharacter.cpp)では、次の順で処理しています。

1. `IsActionLocked()`を確認
2. `CanJump()`を確認し、三回目以降の入力を除外
3. `Jump()`前の`JumpCurrentCount`と`IsFalling()`から二段目か判定
4. `Jump()`を呼ぶ
5. `Jump`または`DoubleJump`へ状態を更新

---

## 2. アミ振り・捕獲

### 完了・確認済み

**ユーザーPIE確認済み**

- アミ振りAnimation Sequence / Montage
- `ABP_Player`のLocomotion後段にFullBody Slotを接続
- ガジェット入力1回につきMontageを1回再生
- Montage終了後にLocomotionへ復帰
- 捕獲成功
- 空振りでも正常終了
- Cooldown中は再使用されない
- 入力連打でMontageや捕獲処理が多重実行されない
- アミ使用中の移動・ジャンプ・ダッシュ・ガジェット切り替えロック
- 終了後に入力が復帰

### C++側の実装

**コード確認済み**

- `EPlayerActionState::UseGadget`
- `ABoarPlayerCharacter::StartGadgetUse()`
- `ABoarPlayerCharacter::FinishGadgetUseAnimation()`
- OneShotガジェットは入力を離した時点では終了せず、Montage完了時に終了
- `bIsGadgetInUse`がMontage完了まで維持される
- `IsActionLocked()`は`bIsStunned || bIsGadgetInUse`
- `EndGadgetUse()`は重複呼び出しに対して早期returnし、終了処理を一度だけ実行
- 中断時も`ANetGadget::EndCaptureWindow()`を呼び、判定を閉じる
- `ANetGadget`はOneShotで、使用開始時にCooldownを開始
- 捕獲判定窓開始時に、既にSphere内にいるイノシシも取得
- 判定窓中に新しくOverlapしたイノシシも取得
- `ABoarBase::IsCaptured()`により同一個体の再捕獲を防止

関連ファイル:

- [BoarPlayerCharacter.cpp](../Private/Player/BoarPlayerCharacter.cpp)
- [NetGadget.cpp](../Private/Gadget/NetGadget.cpp)
- [GadgetBase.cpp](../Private/Gadget/GadgetBase.cpp)
- [AnimNotify_NetCaptureWindow.cpp](../Private/Animation/AnimNotify_NetCaptureWindow.cpp)

### AnimNotify

`UAnimNotify_NetCaptureWindow`をMontage上にON/OFFの2点で配置する構成です。

```text
予備動作       捕獲判定有効          フォロースルー
|-------------|====================|----------------|
              ON                   OFF
```

- ON: `ANetGadget::BeginCaptureWindow()`
- OFF: `ANetGadget::EndCaptureWindow()`

### 網の見た目補正に関する方針

通常時の手Socket位置は維持する方針です。振り下ろし中の足への貫通を補正する必要が残る場合は、SkeletonのSocket自体を動かすのではなく、網Actor内の補正用Scene Component（例: `SwingOffsetRoot`）を一時的に補間する案があります。

ただし大きな補正が必要な場合は、Animation SequenceまたはControl Rigで腕・手首の軌道を直す方を優先します。Anim Notify Stateは補正区間の開始・終了通知として使います。

貫通問題が最終的に解消済みかは、このSourceディレクトリからは確認できていません。必要ならユーザーへ確認してください。

---

## 3. Enhanced Input

### 管理場所

**コード確認済み**

Inputは主に`ABoarPlayerController`で管理しています。

- Mapping Context登録: `ABoarPlayerController::BeginPlay()`
- Input ActionのBind: `ABoarPlayerController::SetupInputComponent()`
- ゲームプレイ処理: Controllerから`ABoarPlayerCharacter`へ委譲

関連ファイル:

- [BoarPlayerController.h](../Public/Player/BoarPlayerController.h)
- [BoarPlayerController.cpp](../Private/Player/BoarPlayerController.cpp)

登録・Bindされている入力:

```text
MoveAction
LookAction
JumpAction
GadgetAction
DashAction
GadgetModifierAction
GadgetSlot1Action
GadgetSlot2Action
GadgetSlot3Action
GadgetSlot4Action
```

Mapping ContextとInput Actionアセットの実体は、PlayerControllerのBlueprint Class Defaultsに設定する構成です。

ガジェット切り替えは、Modifierを押している間だけSlot 1～4を受け付けます。

```text
GadgetModifier Started   -> bIsGadgetModifierHeld = true
GadgetModifier Completed -> bIsGadgetModifierHeld = false
Slot Action              -> TrySwitchGadgetSlot()
```

---

## 4. 捕獲数・ステージクリア

### C++側

**コード確認済み**

[BoarGameMode.cpp](../Private/Core/BoarGameMode.cpp)で次が実装されています。

- 捕獲成功時に檻へ収容
- `CapturedBoarCount`を加算
- 捕獲地点に回復アイテムを確率ドロップ
- `StageConfig->TargetCaptureCount`と現在数を比較
- 目標値が0以下なら捕獲数クリア判定を無効化
- `bStageClearRequested`でクリアイベントの多重発火を防止
- 条件達成時にBlueprintイベント`OnStageCleared()`を呼ぶ

ステージ設定:

- [StageConfig.h](../Public/Stage/StageConfig.h)
- `TargetCaptureCount`

### 確認状況

**ユーザーPIE確認済み**

- 目標捕獲数到達時に`WBP_BoarResult`が表示される
- Retryで現在Levelが再読み込みされる
- 再読み込み後にプレイヤー操作が正常に戻る

---

## 5. リザルト画面

### PIE確認済み（2026-08-28）

- `WBP_BoarResult`の表示
- 捕獲数／目標数の表示
- Retryによる現在Levelの再読み込み
- Retry後のプレイヤー操作復帰
- Detailsで指定した任意名のWidget参照

### C++実装済み（2026-08-28）

- `ABoarGameMode`がクリア成立時に`ABoarPlayerController::HandleStageCleared()`を呼ぶ
- PlayerControllerが移動・視点入力を停止
- CharacterMovementの現在速度を停止
- リザルト表示中はEnhanced Input各入口を明示的に遮断
- `ResultWidgetClass`から`UBoarResultWidget`を生成
- 捕獲数と目標数をWidgetへ設定
- Input ModeをUI Onlyへ変更
- Mouse Cursorを表示
- リトライ時に現在Levelを再読み込み
- Level開始時に移動／視点入力のIgnore状態をResetし、Input ModeをGame Only、Mouse Cursorを非表示へ戻す
- Retry後もPlayerControllerの入力状態が残って操作不能になる不具合を上記初期化で修正（ユーザーPIE確認済み）
- タイトルボタンで設定済みLevelへ遷移
- Widget多重生成とLevel遷移連打を防止
- `UBoarResultWidget`がRetry / Title ButtonをC++でBind
- `UBoarResultWidget`の固定`BindWidget`を廃止し、`WBP_BoarResult`のClass Defaultsにある
  `Result Widget References`で任意名のTextBlock／Buttonを指定する方式へ変更
- 実行時に`WidgetTree`から参照を解決し、必須参照の未設定・型違いはWarning Logへ出力
- 上記変更はUHT、`BoarResultWidget.cpp`、依存ソースのコンパイルまで成功
- 最終DLLリンクのみ、Unreal Editor／Rider LLDBがDLLを使用中のため`LNK1104`で未完了
- UE 5.8 `REBoarGetchEditor Win64 Development`のビルド成功

### Player HUD C++実装済み（2026-08-28）

- `UBoarHUDWidget`を追加
- 現在HP／最大HPのText更新APIを追加
- 現在捕獲数／目標捕獲数のText更新APIを追加
- 固定`BindWidgetOptional`を廃止し、`WBP_PlayerHUD`のClass Defaults > `HUD Widget References`から任意名のTextBlockを指定する方式へ変更
- 上記Details参照方式を含むUE 5.8 Editorのフルビルド成功
- `ABoarPlayerController::BeginPlay()`で常時HUDを生成
- HealthComponentの`OnHealthChanged`を購読し、値変更時だけHUDを更新
- GameModeに`OnCapturedBoarCountChanged`を追加し、捕獲成功時だけHUDを更新
- HUDは`SelfHitTestInvisible`でゲーム入力を妨げない
- リザルト表示時に常時HUDを`Collapsed`へ変更
- UHTと各変更ソースのC++コンパイル成功
- 最終DLLリンクはUnreal Editor／RiderがDLLを使用中だったため`LNK1104`で未完了。Editorとデバッガを閉じて再ビルドすること

### Player HUD PIE確認済み（2026-08-28）

- 開始時のHUD表示と初期HP／最大HP
- ダメージ／回復時のHP即時更新
- 現在捕獲数／目標捕獲数の表示と捕獲時の即時更新
- Result表示時のHUD非表示
- Retry後のHUD再表示と値のリセット

Notionタスク「UI：プレイヤーHP表示」は受け入れ条件を満たしたため`Done`へ更新済みです。

関連ファイル:

- [BoarHUDWidget.h](../Public/UI/BoarHUDWidget.h)
- [BoarHUDWidget.cpp](../Private/UI/BoarHUDWidget.cpp)

関連ファイル:

- [BoarGameMode.cpp](../Private/Core/BoarGameMode.cpp)
- [BoarPlayerController.h](../Public/Player/BoarPlayerController.h)
- [BoarPlayerController.cpp](../Private/Player/BoarPlayerController.cpp)
- [BoarResultWidget.h](../Public/UI/BoarResultWidget.h)
- [BoarResultWidget.cpp](../Private/UI/BoarResultWidget.cpp)

### Blueprint側で次に行うこと

1. Unreal EditorとRiderのデバッグセッションを閉じ、UE 5.8 Editorビルドを完了させる
2. `UBoarHUDWidget`を親にした`WBP_PlayerHUD`を作る
3. Designerに捕獲数／目標数／現在HP／最大HP用のTextBlockを任意名で配置する
4. `WBP_PlayerHUD`のClass Defaults > `HUD Widget References`で各TextBlock名を指定する
   - `Captured Count Text Widget Name`
   - `Target Count Text Widget Name`
   - `Current Health Text Widget Name`
   - `Max Health Text Widget Name`
5. 使用中のPlayerController Blueprintで`Player HUD Widget Class = WBP_PlayerHUD`を設定する
6. `UBoarResultWidget`を親にした`WBP_BoarResult`を作る
7. Designerに捕獲数／目標数用のTextBlockとRetry／Title用のButtonを任意名で配置する
8. `WBP_BoarResult`のClass Defaults > `Result Widget References`で各Widget名を指定する
   - `Captured Count Text Widget Name`: 捕獲数用TextBlock
   - `Target Count Text Widget Name`: 目標数用TextBlock
   - `Retry Button Widget Name`: Retry用Button
   - `Title Button Widget Name`: Title用Button（任意）
   - Result側は固定名も`Is Variable`もC++連携の必須条件ではない
9. 使用中のPlayerController Blueprintで次を設定する
   - `Result Widget Class = WBP_BoarResult`
   - `Title Level Name = タイトルMap名`
10. 使用中のGameModeが、そのPlayerController Blueprintを参照しているか確認する
11. `StageConfig.TargetCaptureCount`を正数に設定する
12. PIEで受け入れ項目を確認する

実装済みフロー:

```text
Event OnStageCleared
  -> ABoarPlayerController::HandleStageCleared
  -> 入力・移動停止
  -> ResultWidget生成
  -> UI Onlyへ切り替え
  -> RetryButtonへFocus
```

### 受け入れ確認

- 目標未達では表示されない
- 目標到達時に一度だけ表示される
- 複数同時捕獲でもWidgetが複数生成されない
- リザルト表示中に移動・ジャンプ・網使用できない
- 捕獲数と目標数が正しく表示される
- リトライで現在Levelが再読み込みされる
- 再読み込み後に入力が正常に戻る
- Mouse / Gamepadの両方で操作できる

---

## 6. 後回し・未完了

### 性能調査: PIEで約20 FPS（2026-08-28、調査継続）

**未修正。CPU/GPUボトルネックを再現時のフレーム時間で確定するまで設定・コードを変更しないこと。**

確認済み:

- 対象Mapは`/Game/Level/Test`、PIEの最大Tick Rateは60
- 実行RHIはD3D12 / SM6、GPUはRTX 4060 Laptop（Driver 596.36）
- Editor起動解像度は2560x1600、Project設定ではLumen、Virtual Shadow Maps、Hardware Ray Tracing、Substrateが有効
- 元のEditorをCodexがバックグラウンド状態で15秒サンプリングした値は、GPU 7～35%・Graphics Clock 210MHz、Editor CPU全体 0.8～2.8%。非フォーカス/待機状態のため、20 FPS再現区間の根拠には使用しない
- 別プロセスの720pスタンドアロン計測では20秒で201フレーム進んだが、同時に`PathTracingMainRG`のRTPSOコンパイル（約85.6秒、その後最大約246秒）とDerived Data Cache 379件待ちが発生。これは通常描画性能ではなく初回シェーダー/PSO生成が混入した無効サンプル
- 元のEditorログでもPIE開始前に`PathTracingMainRG` RTPSOコンパイル（約2秒）が複数記録されている
- C++静的確認では`FStateTreeBoarSenseTask::Tick()`が各イノシシについて毎Tick `RefreshPerceptionTargets()`を呼び、全プレイヤー・全`ACage`走査と条件付きVisibility LineTraceを行う。ただし実測でGameThread支配を確認していないため未修正

次の必須計測（20 FPSが見えている状態でPIEを前面にして採取）:

1. `stat unit`でFrame / Game / Draw / GPUのmsを記録する
2. GPUが最大なら`profilegpu`を採取し、Lumen / Shadows / Ray Tracing / TSR等の内訳を確認する
3. GameまたはDrawが最大ならUnreal Insightsを10～15秒採取し、GameThread / RenderThreadの支配イベントを確認する
4. 解像度を50%へ一時変更したA/B計測を行う。FPSが大きく改善すればGPU側、ほぼ不変ならCPU側の根拠にする
5. シェーダーコンパイル完了後の定常区間だけを比較し、初回PSO生成のヒッチと恒常的な約20 FPSを分離する

現時点の有力候補は高解像度＋Lumen/VSM/RT/SubstrateのGPU負荷、またはイノシシ数に比例するStateTree認識処理だが、いずれも**候補であって原因確定ではない**。

### Stun Montage

ユーザーの希望により後回しです。

C++側の接触ダメージ、スタンTimer、移動停止、無敵時間は存在します。ただし、Stun MontageとABP連携の完成・PIE確認は未記録です。

### Capture / Menu状態

Notion上のプレイヤー状態にはCapture / Menuがあります。

- `EPlayerActionState::Capture`は存在
- Menuは現在の`EPlayerActionState`に存在しない
- Menu UIと入力ロックは未実装または未確認
- Capture状態の表示制御は未完了

### 行動状態の通知

`SetPlayerActionState()`は現在、単純に`CurrentActionState`を書き換えています。状態変更イベントをAnimBP/UIへ通知する仕組みは未完成です。

### 自動テスト

主要機能はPIEで確認されていますが、Automation Test / Functional Testの追加は未記録です。

---

## 7. Notionの進捗

2026-08-17に次を更新済みです。

- [プレイヤー：行動状態とアニメーション連携](https://app.notion.com/p/3a3b6c8875258189a346e32234511619)
  - Locomotion、DoubleJump、アミ振りMontageの進捗を追記
  - Stun / Capture / Menuが残るため`In progress`を維持
- [プレイヤー：二段ジャンプ](https://app.notion.com/p/3a3b6c8875258127a5e3fe52df8b96f0)
  - 受け入れ条件を完了へ更新
- [プレイヤーアニメーション実装ガイド](https://app.notion.com/p/3acb6c88752581ea9fdfd50714c0784b)
  - LocomotionとアミMontageの実装進捗を追記

関連仕様:

- [プレイヤー仕様](https://app.notion.com/p/398b6c887525801ab875fda40262b95c)
- [イノシシ捕獲仕様書](https://app.notion.com/p/398b6c887525800881cbe4f08b3fd64a)
- [アミ使用中の移動・切り替えロック](https://app.notion.com/p/3a3b6c88752581b9b338d990c1a64d23)

Cooldown、空振り、連打を含む総合テストが成功したことは、Notionへまだ反映していません。次にNotionを更新するとき、この完了記録も追記してください。

---

## 8. 新しいタスクで最初に確認すること

1. このファイルの「現在地」と「次にやること」を読む
2. 対象機能のC++を確認する
3. Blueprint / Animationアセットの状態はユーザーへ確認するかUnreal Editorで確認する
4. Notionの対応タスクがある場合は、最新ステータスを取得する
5. 完了済み項目を再実装しない
6. 作業後、このファイルの日付、完了項目、次の作業を更新する

### 更新ルール

- 実装しただけの項目と、PIEで動作確認した項目を区別する
- Blueprint固有情報をC++だけから推測して「完了」と書かない
- 次にやる作業は1つを主推奨として明記する
- 既知の不具合、未決定事項、後回し理由を残す
- Notionを更新した場合は対象ページへのリンクを記録する
