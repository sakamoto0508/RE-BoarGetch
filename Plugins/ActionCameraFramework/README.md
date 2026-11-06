# Action Camera Framework（ACF）

UE 5.8 向けのゲームプレイカメラプラグインです。
Data Asset（Preset）で作ったカメラを「要求」として出すと、優先度とブレンドを管理しながら画面のカメラを切り替えます。
既存の PlayerCameraManager・CameraComponent・SpringArm・Camera Shake はそのまま動きます。
ACF はその上に乗る 1 枚のレイヤーで、PlayerCameraManager（PCM）は置き換えません。

> 使い方と仕様はこの 1 枚にまとめています。`Docs/` は開発の引き継ぎ用の詳細資料で、Git では管理していません。

**現在の状態**:
- Phase 1（Camera Core + Follow / LookAt）完了
- Phase 2（Third Person: Orbital Follow / FreeLook、軸別 Damping、Composer、Collision、Lookahead）完了
- 次は Phase 3（Camera Effects）

---

## 1. 導入

1. `Plugins/ActionCameraFramework` をプロジェクトの `Plugins/` に置きます。
2. `.uproject` の `Plugins` に `{ "Name": "ActionCameraFramework", "Enabled": true }` を追加します。
3. C++ から使う場合は、`Build.cs` に `"ActionCameraFramework"` を追加します。

PlayerCameraManager・GameMode・Character・入力システムの変更は不要です。Enhanced Input にも依存しません。
最初の要求を出すまで、ACF はカメラに一切関与しません。

## 2. 仕組み

```
ゲーム / BP ──RequestCamera(Preset, Params)──▶ Handle         ──AddCameraInput / SetCameraInputRate──▶（入力）
                      │
UACFCameraSubsystem（ローカルプレイヤーごと）
 ├ Director   … どの要求を有効にするか（優先度）と、どう切り替えるか（ブレンド）。入力の消費・時間の前進もここ
 │   └ 要求ごとの RigInstance … Node ごとの実行時状態（Damping・Orbit 角度など）。Preset は共有・不変
 └ OutputModifier … 既存 PCM の CameraModifier（Priority 64）。UE のカメラ結果に ACF の結果を Weight で合成
                      ▼
APlayerCameraManager（既存のまま）→ Camera Shake（127）→ 画面
```

**1 フレームの評価順**（時間は 1 フレームに 1 回だけ進みます）

| 段 | 内容 |
|----|------|
| 1. Context | DeltaTime、UE カメラの姿勢、ControlRotation、アスペクト比、カメラ入力を集めます。入力は時間が進む評価でだけ消費されます |
| 2. 要求の解決 | 優先度で有効な要求を決め、必要なら遷移（ブレンド）を開始します |
| 3. Rig 評価 | **Body**（位置）→ **Aim**（回転）の順に Node を評価します |
| 4. ブレンド | 要求同士のブレンド（Live / Snapshot）と、UE カメラに対する Weight |
| 5. PostBlend | **Collision** をブレンド後の最終姿勢に 1 回だけ適用します。ブレンド中でも壁を抜けません |
| 6. 結果確定 | `Weight`、`AimPose`、デバッグ情報 |
| 7. 出力 | PCM の呼び出しごとに `Lerp(UE カメラ, ACF, Weight)`。この後に UE の Camera Shake がかかります |

- Node は Preset の配列順ではなく、**段ごと**（Body → Aim → PostBlend）に評価されます。配列順が意味を持つのは同じ段の中だけです。
- PCM が Modifier を 1 フレームに複数回呼んでも、時間・Damping・入力が二重に進むことはありません（テストで確認済み）。

## 3. Preset を作る

Content Browser で右クリック > Miscellaneous > Data Asset > **ACFCameraPreset** を選びます。

| Node | 段 | 役割 |
|------|----|------|
| **Fixed Pose** | Body | 位置・回転・FOV を固定値で上書きします（項目ごとに ON / OFF） |
| **Follow** | Body | FollowTarget の位置 + オフセットへ移動します。位置のみ |
| **Orbital Follow** | Body | 対象を中心に旋回します（Third Person / FreeLook）。位置 + 回転 |
| **Position Composer** | Body | 対象が画面上の決まった位置に来るよう、カメラを**平行移動**します（2.5D / 見下ろし向け）。回転はしません |
| **Look At** | Aim | 対象を真っ直ぐ注視します |
| **Rotation Composer** | Aim | 対象が画面上の決まった位置に来るよう、カメラを**回転**します（Dead / Soft Zone あり） |
| **Collision** | PostBlend | ピボットからカメラへ Sphere Sweep し、壁へのめり込みを防ぎます |

**よく使う組み合わせ**
- TPS / アクション: `Orbital Follow` + `Collision`（必要なら `Rotation Composer` で画面上の位置を調整）
- 追従カメラ: `Follow` + `Look At`
- 2.5D / 横スクロール: `Fixed Pose`（回転だけ ON）+ `Position Composer`

位置を決める Node（Follow / Orbital Follow / Position Composer）は 1 つだけにしてください。Data Validation が警告を出します。

### Orbital Follow（FreeLook）の主な設定

| カテゴリ | 項目 | 意味 |
|----------|------|------|
| Pivot | Pivot Offset / Space | 旋回の中心（例: 頭の高さ (0, 0, 60)） |
| Pivot | Pivot Damping（X/Y/Z、World / TargetLocal / CameraYaw） | 中心の追従の遅れ。軸ごとに設定できます |
| Orbit | Distance / Shoulder Offset | アームの長さ / 肩越しのオフセット（SpringArm の TargetArmLength / SocketOffset と同じ意味） |
| Orbit | Pitch Min / Max | 上下の制限 |
| Orbit | Use Rings（Top / Bottom Distance） | FreeLook の 3 リング。Pitch によって距離を変えます |
| Orbit | Follow Target Yaw | ON にすると、対象 Actor の向きに合わせてカメラも回ります |
| Orbit | Rotation Damping（Yaw / Pitch） | 回転の遅れ。軸ごとに設定できます |
| Input | Input Source | Camera Input（ACF の入力 API）/ Control Rotation（SpringArm 相当）/ None |
| Input | Sensitivity / Invert Yaw / Invert Pitch | 感度と反転 |
| Initial | Initial Rotation / Yaw / Pitch | 開始時の向き: 対象の背後 / ControlRotation / 直前のカメラ / 固定 |
| Idle | Recenter When Idle / Delay / Time | 入力が一定時間無ければ、対象の背後へゆっくり戻ります |

`Orbital Follow`（Input Source = Control Rotation）+ `Collision` は、SpringArm の Socket と完全に一致します（実機の BP_PlayerCharacter で誤差 0.000cm / 0.000°）。

### 共通の設定

- **Damping** は「等速で動く対象から何秒分遅れるか」を表します。フレームレートやヒッチに依存しません（30 / 60 / 120fps と不規則なフレームで、解析解と一致することを確認済み）。
- **Target Motion**（Follow / LookAt / Orbit / 2 つの Composer に共通）
  - Lost Behavior: 対象が破棄されたときの動作。HoldLastKnown（最後の位置に留まる、既定）/ PassThrough（この Node を無効にする）
  - Teleport Distance: 1 回の評価でこの距離以上動いたらテレポートとみなし、補間をリセットしてカットします（既定 1000cm、0 で無効）
  - Lookahead
    - Time: 予測する秒数
    - Smoothing: 速度の平滑化時間
    - Direction Change Smoothing Scale: 急な方向転換で古い予測を早く捨てるための係数
    - Max Distance: 予測の上限距離
    - Ignore Vertical: 上下方向の動きを予測に使わない
  - Lookahead の予測位置は、Composer の構図計算と Follow / Orbit の追従先に使われます。
- **Composition**（Composer）
  - 画面座標は中央 (0, 0)、右端 X = +0.5、上端 Y = +0.5 です。
  - Dead Zone の中では補正しません。Soft Zone の中では Damping をかけて Dead Zone へ戻します。Soft Zone の外には決して出しません。
  - 毎フレーム、現在の FOV とアスペクト比で計算するので、FOV やアスペクト比が変わっても構図は保たれます。

### Collision の設定

| 項目 | 意味 |
|------|------|
| Probe Radius / Probe Channel | Sweep する球の半径と、使う Collision Channel（既定 12cm / Camera） |
| Min Distance | ピボットに近づける最小距離 |
| Pull In Damping Time | 障害物に寄るときの補間。既定 0 = 即時（めり込まない） |
| Return Damping Time | 障害物が無くなった後、元の距離へ戻るときの補間 |
| Ignore Pivot Actor / LookAt Actor / Controlled Pawn | 追従対象・注視対象・操作中の Pawn を無視します（追従対象のカプセルにカメラが当たらない） |
| 要求の `CollisionIgnoredActors` | 追加で無視する Actor（要求ごと） |

- ピボットが壁の中から始まる場合は、その物体を無視して Sweep し直します（カメラがピボットに潰れない）。
- カメラの目標位置が壁の中にある場合は、壁の手前に置きます。
- UE の Camera Shake は Collision の後にかかります。Shake の移動量が Probe Radius を超えないようにしてください。
- Phase 3 の ACF 演出は Collision の前に入れる設計です。

### ブレンドの設定（Default Blend In / Out）

| 項目 | 内容 |
|------|------|
| Curve | Cut / Linear / EaseIn / EaseOut / EaseInOut / Custom（UCurveFloat） |
| Duration | 秒 |
| Source | **Live**（既定。切替元のカメラも評価し続ける）/ Snapshot（切替元を止める） |

## 4. 使う

### Blueprint

```
BeginPlay ─▶ Make Target Request Params (Priority, Follow Target = 自分の Pawn, Look At Target, Instigator)
          ─▶ Request Camera For Player (Player Controller, Preset, Params) ─▶ Handle を保存
入力       ─▶ Get ACF Camera Subsystem ─▶ Add Camera Input (X = Yaw, Y = Pitch)
               ※スティックは Set Camera Input Rate を毎フレーム呼びます（DeltaTime が掛かります）
終了時     ─▶ Release Camera For Player (Player Controller, Handle)
```

Subsystem から使えるノード:
- `Set Camera Targets`
- `Set Orbit Rotation`
- `Notify Teleport`
- `Release All Cameras`
- `Is Active Camera`
- `Get Last Evaluation Result`
- `Get Aim / Visual Camera Pose`
- `Suppress Output`

### C++

```cpp
UACFCameraSubsystem* Cameras = UACFCameraBlueprintLibrary::GetCameraSubsystem(PlayerController);
FACFCameraRequestParams Params;
Params.FollowTarget.Actor = PlayerPawn;     // Actor / SceneComponent / Socket を指定できる。弱参照
Params.LookAtTarget.Actor = Enemy;          // 追従対象とは別の対象を指定できる
Params.Instigator = this;                   // this が破棄されると自動で解除
CameraHandle = Cameras->RequestCamera(TPSPreset, Params);

// 入力（Enhanced Input / 旧 Input / AI / UI など、何からでも）
Cameras->AddCameraInput(FVector2D(MouseDelta.X, MouseDelta.Y));
Cameras->SetCameraInputRate(StickValue * 120.f);   // 毎フレーム呼ぶ

Cameras->NotifyTeleport();                   // リスポーンなど: 補間をリセットしてカット
Cameras->ReleaseCamera(CameraHandle);
```

**入力のルール**
- `AddCameraInput` はデルタとして蓄積されます。`SetCameraInputRate` は毎秒あたりの量で、DeltaTime が掛けられます。
- どちらも、次に時間が進むカメラ評価で 1 回だけ消費されます。DeltaTime が 0 の評価や、同じフレームの再呼び出しでは消費されません。
- Rate は消費されると 0 に戻るので、毎フレーム呼び直してください。

## 5. ルール

| 事象 | 挙動 |
|------|------|
| 優先度 | Priority が大きい要求が有効になります。同じ値なら後から出した要求が勝ちます |
| 切替 / 最後の要求を解除 | 新しい要求の BlendIn / 解除した要求の BlendOut を使います |
| ブレンド中の割り込み | その時点の画面の姿勢から新しいカメラへブレンドします。画面は飛びません |
| Instigator が破棄 | 次のフレームで自動的に解除されます |
| 追従・注視対象が破棄 | 要求は残り、Lost Behavior に従います。クラッシュはしません |
| 対象を差し替え / テレポート | その Node の補間をリセットします。カメラはレベルを滑らずにカットします |
| PlayerController や PCM の作り直し | ACF が次のティックまでに自動で接続し直します |

## 6. 他のシステムとの関係

- **Camera Shake**: そのまま動き、ACF と Collision の後にかかります。
- **Sequencer など**: 原則として ACF の出力を抑えます。Phase 1〜2 では自動検出しないので、`Suppress Output (Source=Self)` と `Release Output Suppression` を自分で呼んでください。抑制中も ACF は内部で評価を続けます。
- **照準と見た目**:
  - `Get Aim Camera Pose`: UE カメラ + ACF。Shake・演出を含みません。照準やトレースに使います。
  - `Get Visual Camera Pose`: 実際に描画されるカメラです。
- **ネットワーク**: ACF はカメラ情報をサーバーへ送りません。

## 7. 設定・デバッグ

- **Project Settings > Plugins > Action Camera Framework**: Output Modifier Class、Log Requests
- **`ACF.Debug.Draw 1`**: ワールドに描画します。
  - 追従対象（緑）、Lookahead の予測位置（水色）、注視点（黄）
  - Collision の Sweep（ヒット時は赤）、カメラ
- **`ACF.Debug.Draw 2`**: 上の描画に加えて、画面に表示します。
  - Dead Zone（緑枠）、Soft Zone（赤枠）、画面上の対象位置
  - カメラ位置・回転、Orbit の Yaw / Pitch / 距離、Collision の状態
- **`showdebug camera`**: ACF の要求、Weight、抑制状態を表示します。
- **コンソール**: `ACF.Debug.PushFixed`、`ACF.Debug.ReleaseLast`、`ACF.Debug.ReleaseAll`、`ACF.Debug.Dump`
- デバッグ処理は、Shipping ビルドではコンパイル自体から除外されます。

## 8. 拡張（プログラマー向け）

`UACFCameraNode` を継承し、`GetStage()`（Body / Aim / PostBlend）と `Evaluate` を実装します。

- Node のメンバーは共有設定です。実行時に書き換えないでください。
- フレームごとに変わる値は USTRUCT に入れ、`GetRuntimeStateType()` で返します。
- テレポートには `ResetInterpolation`、有効化には `OnActivated` で対応します。
- `DeltaTime <= 0` のときは状態を進めないでください。
- Body の Node は `Context.Scratch.Pivot` を設定してください（Collision の Sweep 起点になります）。
- 共通部品: `FACFCameraTargetTrackingState::Track`（対象喪失・テレポート・Lookahead）、`UpdateDamped`（軸別 Damping）、`ACFFraming`（画面への投影・構図）

## 9. 制限（Phase 2 時点）

- 描画結果の目視確認、エディタ UI での手操作確認、実機パッドでの操作感確認は、まだ行っていません（自動テストは NullRHI で実行）。
- Camera Shake と Collision の組み合わせ（Shake で壁に寄る量）は未検証です。設計上は、Probe Radius を余裕として確保しています。
- ブレンド中に割り込むと、割り込まれた側の動きはそれ以降反映されません。
- Collision は「ピボット → カメラ」の線上だけを見ます。視線上の遮蔽物を避ける Deoccluder は Phase 4 です。
- Sequencer の自動検出、Blueprint 製の Node は未対応です。

## 10. 検証

- `Content/Verification/` の検証用アセット（DA ×3、BP ×2）は `Scripts/CreateVerificationAssets.py` で作り直せます（既存のアセットは再利用し、グラフだけ書き直します）。
- 自動テストは 33 件です。
  - `.Director.*` / `.Nodes.*` / `.ThirdPerson.*`: Editor とゲームの両方で実行できます。
  - `.Integration.*`: `-game` でのみ実行されます。

```bash
UnrealEditor-Cmd.exe <project>.uproject -unattended -NullRHI -nosplash "-ExecCmds=Automation RunTests ActionCameraFramework.Director+ActionCameraFramework.Nodes+ActionCameraFramework.ThirdPerson" "-TestExit=Automation Test Queue Empty"
```

```bash
MSYS_NO_PATHCONV=1 UnrealEditor-Cmd.exe <project>.uproject /Game/Level/L_Lobby -game -unattended -NullRHI -nosplash -nosound "-ExecCmds=Automation RunTests ActionCameraFramework" "-TestExit=Automation Test Queue Empty"
```

## 11. ファイル構成

| パス | 内容 |
|------|------|
| `Source/ActionCameraFramework/Public/` | `ACFCameraSubsystem`（入口）、`ACFCameraDirector`、`ACFCameraPreset`、`ACFCameraNode`（段・Scratch）、`ACFCameraRigInstance`、`ACFCameraOutputModifier`、`ACFCameraTypes`、`ACFCameraSettings`、`ACFCameraBlueprintLibrary` |
| `Source/ActionCameraFramework/Public/Nodes/` | FixedPose / Follow / LookAt / OrbitalFollow / Composers / Collision と、共通部品の TargetTracking（Damping・Lookahead・テレポート）/ Framing（画面座標の計算） |
| `Source/ActionCameraFramework/Private/` | 上記の実装、`ACFCameraDebugDraw`（Shipping では除外）、`Tests/` |
| `Source/ActionCameraFrameworkEditor/` | Editor モジュール（Phase 6 のツール置き場） |
| `Content/Verification/` / `Scripts/` | 検証用アセットと、その生成スクリプト |
| `Docs/`（Git 管理外） | 引き継ぎ用の詳細資料 |
