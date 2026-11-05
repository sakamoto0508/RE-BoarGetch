# Action Camera Framework（ACF）

UE 5.8 向けのゲームプレイカメラプラグインです。
Data Asset（Preset）で作ったカメラを「要求」として出すと、優先度とブレンドを管理しながら画面のカメラを切り替えます。
既存の PlayerCameraManager・CameraComponent・SpringArm・Camera Shake はそのまま動きます。
ACF はその上に乗る 1 枚のレイヤーで、PlayerCameraManager（PCM）は置き換えません。

> 使い方と仕様はこの 1 枚にまとめています。`Docs/` は開発の引き継ぎ用の詳細資料で、Git では管理していません。

**現在の状態**: Phase 1（Camera Core + Follow / LookAt）完了。次は Phase 2（Third Person Camera）。

---

## 1. 導入

1. `Plugins/ActionCameraFramework` をプロジェクトの `Plugins/` に置きます。
2. `.uproject` の `Plugins` に `{ "Name": "ActionCameraFramework", "Enabled": true }` を追加します。
3. C++ から使う場合は、`Build.cs` に `"ActionCameraFramework"` を追加します。

PlayerCameraManager・GameMode・Character の変更は不要です。最初の要求を出すまで、ACF はカメラに一切関与しません。

## 2. 仕組み（30 秒で）

```
ゲーム / BP ──RequestCamera(Preset, Params)──▶ Handle
                      │
UACFCameraSubsystem（ローカルプレイヤーごと）
 ├ Director   … どの要求を有効にするか（優先度）と、どう切り替えるか（ブレンド）を決める
 │   └ 要求ごとの RigInstance … Node ごとの実行時状態（Damping など）。Preset は共有・不変
 └ OutputModifier … 既存 PCM の CameraModifier（Priority 64）。UE のカメラ結果に ACF の結果を Weight で合成
                      ▼
APlayerCameraManager（既存のまま）→ Camera Shake（127）→ 画面
```

- **Preset（Data Asset）** には共有できる設定だけを入れます。実行時の状態は要求ごとに別々に持つので、複数のプレイヤーが同じ Preset を使っても状態は混ざりません。
- **Node** は上から順に評価されます。最初の Node は UE カメラの姿勢を受け取るので、必要な項目だけを上書きできます。
- 時間（ブレンド・Damping）は **1 フレームに 1 回だけ** 進みます。PCM が Modifier を 1 フレームに複数回呼んでも、二重に進むことはありません。
- 要求が無い状態は「UE カメラそのまま」です。ACF への切替と UE カメラへの戻りは、どちらも Weight のブレンドで行います。

## 3. Preset を作る

Content Browser で右クリック > Miscellaneous > Data Asset > **ACFCameraPreset** を選びます。

| Node | 役割 | 主な設定 |
|------|------|----------|
| **Fixed Pose** | 位置・回転・FOV を固定値で上書きします（項目ごとに ON / OFF） | Location / Rotation / Field Of View |
| **Follow** | 要求の FollowTarget の位置へ移動します（位置のみ） | Offset、Offset Space（World / Target Local）、Damping Time、Target Lost Behavior |
| **Look At** | 対象を注視します（回転のみ、Roll 0） | Target Source（Look At Target / Follow Target）、Target Offset、Damping Time、Target Lost Behavior |

典型的な追従カメラは、`Follow`（Offset (-350, 0, 180)、Target Local、Damping 0.2）の下に `Look At`（Target Source = Follow Target、Offset (0, 0, 60)）を置きます。

ブレンドは **Default Blend In / Default Blend Out** で設定します。

| 項目 | 選択肢 | 意味 |
|------|--------|------|
| Curve | Cut / Linear / EaseIn / EaseOut / EaseInOut / Custom（UCurveFloat） | 補間の形 |
| Duration | 秒 | 0 は Cut と同じ |
| Source | **Live**（既定）/ Snapshot | Live: 切替元のカメラも動き続けます。Snapshot: 切替元を開始時点で止めます |

- **Damping Time** は「等速で動く対象から何秒分遅れるか」を表します。フレームレートで結果が変わりません（30 / 60 / 120fps で一致することをテストで確認済み）。
- **Target Lost Behavior** は対象が破棄されたときの動作です。
  - HoldLastKnown（既定）: 最後に見えた位置に留まります。
  - PassThrough: その Node が無効になり、UE カメラの値が見えます。

## 4. 使う

### Blueprint

```
BeginPlay ─▶ Request Camera For Player (Player Controller, Preset, Params) ─▶ Handle を変数に保存
終了時    ─▶ Release Camera For Player (Player Controller, Handle)   ※Handle は参照渡しで、解除後に無効になります
```

- `Params`（Make ACFCameraRequestParams）で指定できる項目:
  - Priority
  - Instigator: 指定した Object が破棄されると自動で解除されます。
  - Follow Target / Look At Target: Actor、SceneComponent、Socket を指定でき、それぞれ別の対象にできます。
  - Blend In の上書き
- `Get ACF Camera Subsystem` から使えるノード: `Set Camera Targets`、`Release All Cameras`、`Is Active Camera`、`Get Last Evaluation Result`、`Get Aim / Visual Camera Pose`、`Suppress Output`

### C++

```cpp
#include "ACFCameraSubsystem.h"
#include "ACFCameraBlueprintLibrary.h"

if (UACFCameraSubsystem* Cameras = UACFCameraBlueprintLibrary::GetCameraSubsystem(PlayerController))
{
	FACFCameraRequestParams Params;
	Params.Priority = 10;
	Params.Instigator = this;                       // this が破棄されると自動で解除
	Params.FollowTarget.Actor = PlayerPawn;
	Params.LookAtTarget.Actor = Enemy;              // 追従対象とは別に指定できる
	CameraHandle = Cameras->RequestCamera(ChasePreset, Params);
	// ...
	Cameras->ReleaseCamera(CameraHandle);           // 自分の要求だけを解除。二重に解除しても安全
}
```

## 5. ルール

| 事象 | 挙動 |
|------|------|
| 優先度 | Priority が大きい要求が有効になります。同じ値なら後から出した要求が勝ちます |
| 切替 | 新しく有効になる要求の BlendIn を使います |
| 最後の要求を解除 | その要求の BlendOut で UE カメラへ戻ります（`Release Camera With Blend` で指定も可） |
| ブレンド中の割り込み | その時点の画面の姿勢（スナップショット）から新しいカメラへブレンドします。画面は飛びません |
| 待機中（下位）の要求を解除 | 画面は変わりません |
| Instigator が破棄 | 次のフレームで自動的に解除されます |
| 追従・注視対象が破棄 | 要求は残り、Node の Target Lost Behavior に従います。クラッシュはしません |
| 対象を差し替え（`SetCameraTargets`） | 対象が別のオブジェクトになった Node は、Damping をリセットしてスナップします |
| null Preset / 不明な Handle | 要求は拒否されます / 解除は何もしません（false を返します） |
| PlayerController や PCM の作り直し | ACF が次のティックまでに自動で接続し直します |

## 6. 他のシステムとの関係

- **Camera Shake**: そのまま動き、ACF の出力の上にかかります。
- **Sequencer など別システムにカメラを渡す**: 原則として ACF の出力を抑えます。Phase 1 では自動検出しないので、自分で呼んでください。
  ```
  Sequence On Play ─▶ Suppress Output (Source=Self, Blend Time=0)
  Sequence On Stop ─▶ Release Output Suppression (Source=Self, Blend Time=0.5)
  ```
  抑制中も ACF は内部で評価を続けるので、解除すると最新の状態から戻ります。Source が破棄された場合は自動で解除されます。
- **照準（Aim）と見た目（Visual）**:
  - `Get Aim Camera Pose`: UE カメラ + ACF。Shake や演出を含みません。射撃やトレースに使います。
  - `Get Visual Camera Pose`: 実際に描画されるカメラです。
- **ネットワーク**: ACF はカメラ情報をサーバーへ送りません。照準をサーバーに渡す場合は、ゲーム側で Aim Pose を送ってください。
- **他の CameraModifier**: Priority が 64 より小さいものは ACF より先に、大きいものは後に適用されます。ACF の順番を変える場合は、`UACFCameraOutputModifier` のサブクラスを作って Project Settings に設定します。

## 7. 設定・デバッグ

- **Project Settings > Plugins > Action Camera Framework**
  - Output Modifier Class
  - Log Requests: 要求の追加・解除・切替を `LogActionCamera` に出力します。
- **`showdebug camera`**: ACF の有効な要求、Weight、抑制状態、姿勢を表示します。
- **コンソール（Shipping 以外）**
  - `ACF.Debug.PushFixed X Y Z Pitch Yaw Roll FOV [Priority] [BlendSeconds]`
  - `ACF.Debug.ReleaseLast [Blend]`
  - `ACF.Debug.ReleaseAll [1=即時]`
  - `ACF.Debug.Dump`

## 8. 拡張（プログラマー向け）

新しいカメラの挙動は、`UACFCameraNode` を継承して `Evaluate` を実装するだけで追加できます。

- Node のメンバーは共有設定です。実行時に書き換えないでください。
- フレームごとに変わる値は USTRUCT に入れ、`GetRuntimeStateType()` で返します。要求ごとに 1 度だけ確保され、`Evaluate` の `FInstancedStruct*` として渡されます。
- `DeltaTime <= 0` のときは状態を進めないでください（フレームガードの前提です）。

## 9. 制限（Phase 1 時点）

- 提供している Node は Fixed Pose / Follow / Look At だけです。Orbit・Composer・Collision・Lookahead は Phase 2 で追加します。
- Damping の時定数は 1 つだけです（軸ごとの設定は Phase 2）。
- ブレンド中に割り込むと、割り込まれた側の動きはそれ以降反映されません。
- Sequencer の自動検出はまだありません（6 章の API を呼んでください）。
- Blueprint で Node を作ることはできません（C++ のみ）。
- 描画結果の目視確認とエディタ UI での手操作確認は、まだ行っていません（自動テストは NullRHI で実行）。

## 10. 検証

- `Content/Verification/` の検証用アセット（DA ×2、BP ×1）は `Scripts/CreateVerificationAssets.py` で作り直せます。
- 自動テストは 17 件です。
  - `ActionCameraFramework.Director.*` / `.Nodes.*`: Editor とゲームの両方で実行できます。
  - `.Integration.*`: `-game` でのみ実行されます。

```bash
UnrealEditor-Cmd.exe <project>.uproject -unattended -NullRHI -nosplash "-ExecCmds=Automation RunTests ActionCameraFramework.Director+ActionCameraFramework.Nodes" "-TestExit=Automation Test Queue Empty"
```

```bash
MSYS_NO_PATHCONV=1 UnrealEditor-Cmd.exe <project>.uproject /Game/Level/L_Lobby -game -unattended -NullRHI -nosplash -nosound "-ExecCmds=Automation RunTests ActionCameraFramework" "-TestExit=Automation Test Queue Empty"
```

## 11. ファイル構成

| パス | 内容 |
|------|------|
| `Source/ActionCameraFramework/Public/` | `ACFCameraSubsystem`（入口）、`ACFCameraDirector`、`ACFCameraPreset`、`ACFCameraNode`、`ACFCameraRigInstance`、`ACFCameraOutputModifier`、`ACFCameraTypes`、`ACFCameraSettings`、`ACFCameraBlueprintLibrary`、`Nodes/`（FixedPose / Follow / LookAt / TargetTracking） |
| `Source/ActionCameraFramework/Private/Tests/` | 自動テスト |
| `Source/ActionCameraFrameworkEditor/` | Editor モジュール（Phase 6 のツール置き場。現在は空） |
| `Content/Verification/` / `Scripts/` | 検証用アセットと、その生成スクリプト |
| `Docs/`（Git 管理外） | 引き継ぎ用の詳細資料: 要件・設計判断・ライフサイクル・実装計画 |
