# Action Camera Framework（ACF）

UE 5.8 向けのゲームプレイカメラプラグインです。
Data Asset（Preset）で作ったカメラを「要求」として出すと、優先度とブレンドを管理しながら画面のカメラを切り替えます。
既存の PlayerCameraManager・CameraComponent・SpringArm・Camera Shake はそのまま動きます。
ACF はその上に乗る 1 枚のレイヤーで、PlayerCameraManager（PCM）は置き換えません。

> 使い方と仕様はこの 1 枚にまとめています。`Docs/` は開発の引き継ぎ用の詳細資料で、Git では管理していません。

**現在の状態**:
- Phase 1（Camera Core + Follow / LookAt）完了
- Phase 2（Third Person: Orbital Follow / FreeLook、軸別 Damping、Composer、Collision、Lookahead）完了。最終監査で出力 Guard を追加
- Phase 3（Camera Effects: Punch / Recoil / Impulse / ACF Shake / Speed FOV / Roll、Effect Preset、Handle）完了
- Phase 4A（Lock-On、Target Group / Group Framing）完了
- 次は Phase 4B（Action Camera の残り）

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
| 3. Rig 評価 | **Body**（位置）→ **Aim**（回転）→ **Framing**（視線方向の移動・FOV で対象を画面に収める）の順に Node を評価します |
| 4. ブレンド | 要求同士のブレンド（Live / Snapshot）と、UE カメラに対する Weight |
| 5. 演出と PostBlend | 位置の演出 → **Collision**（ブレンド後の最終姿勢に 1 回）→ 回転・FOV の演出。ブレンド中や演出中でも壁を抜けません |
| 6. 結果確定 | `Weight`、`AimPose`、デバッグ情報 |
| 7. 出力 | PCM の呼び出しごとに `Lerp(UE カメラ, ACF, Weight)`。この後に UE の Camera Shake がかかり、最後に Guard が最終位置を Collision で再拘束します |

- Node は Preset の配列順ではなく、**段ごと**（Body → Aim → Framing → PostBlend）に評価されます。配列順が意味を持つのは同じ段の中だけです。
- PCM が Modifier を 1 フレームに複数回呼んでも、時間・Damping・入力が二重に進むことはありません（テストで確認済み）。

## 3. Preset を作る

Content Browser で右クリック > Miscellaneous > Data Asset > **ACFCameraPreset** を選びます。

| Node | 段 | 役割 |
|------|----|------|
| **Fixed Pose** | Body | 位置・回転・FOV を固定値で上書きします（項目ごとに ON / OFF） |
| **Follow** | Body | FollowTarget の位置 + オフセットへ移動します。位置のみ |
| **Orbital Follow** | Body | 対象を中心に旋回します（Third Person / FreeLook）。位置 + 回転 |
| **Position Composer** | Body | 対象が画面上の決まった位置に来るよう、カメラを**平行移動**します（2.5D / 見下ろし向け）。回転はしません |
| **Lock-On** | Body | プレイヤー（Follow Target）の背後、ロック対象（Look At Target）と反対側に回り込み、両方を見ます。位置 + 回転 |
| **Look At** | Aim | 対象を真っ直ぐ注視します |
| **Rotation Composer** | Aim | 対象が画面上の決まった位置に来るよう、カメラを**回転**します（Dead / Soft Zone あり） |
| **Group Framing** | Framing | 複数の対象（球）が画面の指定範囲に収まるよう、カメラを**視線方向に前後**させるか **FOV** を変えます。回転はしません |
| **Collision** | PostBlend | ピボットからカメラへ Sphere Sweep し、壁へのめり込みを防ぎます |

Look At と Rotation Composer の **Target Source** は、Look At Target / Follow Target / **Target Group**（グループの重み付き中心）から選べます。

**よく使う組み合わせ**
- TPS / アクション: `Orbital Follow` + `Collision`（必要なら `Rotation Composer` で画面上の位置を調整）
- 追従カメラ: `Follow` + `Look At`
- 2.5D / 横スクロール: `Fixed Pose`（回転だけ ON）+ `Position Composer`
- ロックオン: `Lock-On` + `Group Framing`（Follow / Look At Target Subject を ON）+ `Collision`（サンプル `DA_ACFSample_LockOn`）
- 複数対象: `Orbital Follow` + `Rotation Composer`（Target Source = Target Group）+ `Group Framing` + `Collision`（サンプル `DA_ACFSample_Group`）

位置を決める Node（Follow / Orbital Follow / Lock-On / Position Composer）は 1 つだけにしてください。Data Validation が警告を出します（Group Framing の重複、対象が 1 つも設定されていない Group Framing も警告します）。

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

### Lock-On の設定

プレイヤー = 要求の **Follow Target**、ロック対象 = **Look At Target** です（Actor / SceneComponent / Socket）。

| カテゴリ | 項目 | 意味 |
|----------|------|------|
| Player | Pivot Offset / Pivot Damping / Target Motion | 旋回の中心（プレイヤー + オフセット）、その追従の遅れ、テレポート検出・Lookahead |
| Lock Target | Lock Target Offset | ロック点（例: 胸の高さ） |
| Lock Target | Lock Target Damping Time | ロック点の追従時間。**ターゲット切替**や対象の瞬間移動はこの時間で滑らかに移ります（既定 0.25 秒） |
| Lock Target | Min Horizontal Distance | プレイヤーと対象の水平距離がこれ未満なら Yaw を保持します（真上・重なりで回転しない） |
| Orbit | Shoulder Offset | 肩越しのオフセット。プレイヤーが対象を隠さないようにします |
| Orbit | Base Pitch / Elevation Pitch Scale / Pitch Min・Max | 基本の俯角と、対象の高さ（仰角）を Pitch に反映する割合。大型の敵ではカメラが下がって見上げます |
| Orbit | Rotation Damping（Yaw / Pitch） | 回り込みの遅れ |
| Range | Near / Far Range、Near / Far Arm Length | プレイヤーと対象の距離に応じたアーム長（間は線形）。既定は近いほど遠く引きます |
| Range | Adjust Field Of View（Near / Far FOV） | 距離に応じて FOV も変えます（既定 OFF） |
| Range | Range Damping Time | 距離による変化の遅れ（ポンピング防止） |
| Aim | Aim At Focus / Focus Bias / Focus Screen Position | プレイヤー（0）とロック点（1）の間の注視点と、その画面位置。OFF にすると Aim の Node に回転を任せます |

**構図の計算**: Orbit の Yaw = プレイヤー → ロック点の水平方向、Pitch = Base Pitch + 仰角 × Scale。カメラ = ピボット − 向き × アーム長 + 肩オフセット。回転は注視点を Focus Screen Position に置く向き（`ACFFraming::AimAt`）。
両方を**確実に**画面に収めるのは Group Framing の役目です（ロック点は Lock-On Node が平滑化した位置を Group Framing へ渡すので、ターゲット切替中に構図が跳びません）。

### Group Framing（Target Group）の設定

対象は要求の **Target Group**（メンバーごとに Target・Offset・**Weight**・**Radius**）と、任意で Follow / Look At Target です。

| 項目 | 意味 |
|------|------|
| Frame Target Group / Follow Target Subject / Look At Target Subject | 収める対象。Follow / Look At は Include を ON にし、Radius・Weight・Offset を設定します |
| Mode | **Distance**（視線方向に前後）/ **Field Of View**（ズーム）/ **Distance Then Field Of View**（既定。前後の上限を超えた分を FOV で補う） |
| Policy | **Keep Inside**（既定。はみ出しそうなときだけ引く・広げる。元のカメラより寄らない）/ **Fill**（範囲いっぱいに寄せる・ズームする） |
| Framing Size | 対象の球を収める画面の範囲（1 = 画面全体、中央基準。既定 0.8） |
| Max Dolly In / Out | 前後に動かせる最大距離（In は Fill のときだけ） |
| Min / Max Field Of View、Near Clearance | FOV の範囲、カメラ前方に必要な余裕 |
| Widen / Narrow Damping Time | 引く・広げるとき（速い、既定 0.15 秒）/ 寄る・狭めるとき（ゆっくり、既定 0.8 秒）の遅れ |

**計算方法**: Body と Aim の後（回転が確定した後）に、各対象の球について「画面の枠（Framing Size）の 4 辺の平面に接する」ために必要な**後退距離**、または**半画角**を解析的に求め、最も厳しい値を使います（重心だけを追うのではなく、全員の球が枠に入ることを保証します）。
- **Weight**: 重み付き中心を求め、各メンバーを `Weight / 最大 Weight` の割合で中心から離れた位置・半径として扱います。軽いメンバーは多少はみ出してもよい扱いになり、Weight 0 は無視されます（グループには残る）。
- **0 件**: 補正なし（直前の補正は Narrow Damping で滑らかに戻ります）。**1 件**: その球だけを収めます（Radius が効きます）。
- 補正は「入ってきたカメラからの差分」として保持するので、累積せず、前段の Damping とも干渉しません。
- 破棄されたメンバーは Director が毎フレーム自動で取り除きます（メモリは再確保しません）。

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

**ロックオン**（対象の選択・優先順位・切替の判断はゲーム側の責務です。ACF はカメラだけを担当します）

```
ロック開始 ─▶ Make ACF Lock-On Request Params (Priority = 10, Player, Lock Target, Release When Target Lost = true)
           ─▶ Request Camera For Player (Player Controller, DA_ロックオン用Preset, Params) ─▶ LockHandle を保存
ターゲット切替 ─▶ Set ACF Camera Lock-On Actor (LockHandle, 新しい対象)      … カメラは滑らかに回り込む
ロック解除 ─▶ (任意) Set Control Rotation ← Get Aim Camera Pose の回転    … 通常カメラが元の向きへ戻らないように
           ─▶ Release Camera For Player (LockHandle)                        … 通常カメラの Blend In で戻る
```

- 通常カメラ（Priority 0）を出したまま、ロックオン用 Preset を高い Priority で要求します。開始・解除は通常のカメラ間ブレンドです。
- `Release When Target Lost` が ON なら、ロック中の対象が破棄されると自動で解除されます（切替後も新しい対象を見ます）。
- ロックオン中も、移動入力の基準（ControlRotation）はゲーム側のものです。カメラの向きを使いたい場合は `Get Aim Camera Pose` を使ってください。

**Target Group**

| ノード | 内容 |
|--------|------|
| `Add ACF Camera Group Actor` (Handle, Actor, Weight, Radius) / `Add ACF Camera Group Member` (Handle, Member) | 追加（同じ対象なら Weight・Radius・Offset を更新） |
| `Remove ACF Camera Group Actor` / `Remove ACF Camera Group Member` | 削除 |
| `Set ACF Camera Group Members` (Handle, Members) / `Get ACF Camera Group Members` | 一括設定（空配列でクリア）/ 現在のメンバー |
| `Make ACF Camera Group Member` (Actor, Weight, Radius, Offset) | メンバーの作成 |

要求を出す前に `Params.TargetGroup.Members` に入れておくこともできます。

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

// ロックオン
FACFCameraRequestParams LockParams = UACFCameraBlueprintLibrary::MakeLockOnRequestParams(10, PlayerPawn, Enemy);
LockHandle = Cameras->RequestCamera(LockOnPreset, LockParams);
Cameras->SetCameraLockOnActor(LockHandle, NextEnemy);              // 切替
Cameras->ReleaseCamera(LockHandle);

// Target Group
Cameras->AddCameraGroupActor(GroupHandle, Boss, /*Weight*/ 2.f, /*Radius*/ 250.f);
Cameras->RemoveCameraGroupActor(GroupHandle, Boss);
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
| 対象を差し替え / テレポート | その Node の補間をリセットします。カメラはレベルを滑らずにカットします（Lock-On のロック対象だけは例外で、切替・瞬間移動とも Lock Target Damping Time で滑らかに回り込みます。プレイヤー側のテレポートはカットします） |
| ロック対象が破棄 | 最後の位置を見続けます。`bReleaseWhenLookAtTargetLost` が ON なら、要求が自動で解除されます |
| ロックオンの解除 | 戻り先のカメラの BlendIn で戻ります（カメラ間の切替なので、解除した要求の BlendOut は UE カメラへ戻るときだけ使われます） |
| Target Group のメンバーが破棄 | 次の評価でグループから自動的に取り除かれます |
| PlayerController や PCM の作り直し | ACF が次のティックまでに自動で接続し直します |

## 6. カメラ演出（Effects）

Punch / Recoil / Impulse / Shake / Speed FOV / Roll を、ゲームのイベントから 1 ノードで再生できます。
演出は ACF カメラにかかります。UE 標準のカメラだけを使っている場面で演出を出したい場合は、Node が Collision だけの「素通し Preset」を要求してください。UE カメラがそのまま映り、その上に演出と Collision がかかります。

**Effect Preset を作る**: Data Asset > **ACFCameraEffectPreset** を作り、`Layers` に演出を追加します。1 つの Preset に複数の演出を入れることができ、まとめて 1 つの Handle で再生・停止されます（例: 重い着地 = Impulse + Shake + FOV Punch）。

| Layer | チャンネル | 内容 |
|-------|-----------|------|
| **Camera Punch** | 位置 / 回転 / FOV | 瞬間的なキック。ダッシュ時の FOV の演出にも使えます |
| **Camera Recoil** | 位置（後退・上）/ 回転（Pitch、Yaw・Roll はランダム） | 攻撃の反動。連射すると加算されます。ばらつきは Seed で決まる（再現できる） |
| **Camera Impulse** | 位置 / 回転 | 爆発・着地・被弾。発生源から離れる方向へ押し、Frequency > 0 なら減衰しながら振動します。距離で減衰します |
| **Camera Shake (UE)** | 位置 / 回転 / FOV | UE の `UCameraShakeBase` クラスをそのまま使います（Wave / Perlin / Sequence など） |
| **Speed FOV** | FOV | 追従対象の速度に応じて FOV を広げます（Min / Max Speed、増加量、Min / Max FOV、上がる・戻る速さ） |
| **Camera Roll** | 回転（Roll） | 旋回や横移動に応じてカメラを傾けます（最大角度、反応と戻りの速さ） |

**全 Layer に共通の設定**

| 項目 | 内容 |
|------|------|
| Envelope | Attack / Sustain（負の値 = 止めるまで続く）/ Release の秒数、形（Linear / Smooth） |
| Weight / Priority | 強さ / Override の適用順（大きい方が後に適用され、上に乗る） |
| Blend Mode | **Additive**（足す）/ **Override**（その値へ Weight で寄せる）/ **Multiply**（FOV の倍率） |
| Intensity Curve | 経過秒数に対する強さの倍率（任意） |
| Distance Attenuation | 発生源からの距離による減衰（Inner Radius まで 100%、Outer Radius で 0%、Falloff カーブは任意） |

**合成の順番**（チャンネルごとに、すべての演出をまとめて適用します）
- 位置・回転: Override（Priority の小さい順）→ Additive
- FOV: Override → Multiply → Additive
- 演出による FOV は Project Settings の Effect FOV Min / Max（既定 20〜130）に収まります。元の FOV がすでにこの範囲の外にある場合は、その値をさらに外へ押し出すことはしません。

**Pipeline**

```
Rig → ブレンド → [PreCollision] 位置の演出 → Collision → [PostCollision] 回転・FOV の演出 → Output → UE Shake → Guard
```
- 位置を動かす演出（Punch / Recoil / Impulse / Shake の位置成分）は Collision の前にかかるので、壁を抜けません（Guard なしで確認済み）。
- 回転・FOV の演出は Collision の後にかかるので、壁際でも効きます。
- `Get Aim Camera Pose` は位置の演出（Collision 済み）を含み、回転・FOV の演出を含みません。そのため Recoil の Pitch や Shake の回転で照準は揺れません。

**Blueprint**（UE 標準の Camera Shake ノードと区別できるよう、名前に「ACF」を付けています）

| ノード | 内容 |
|--------|------|
| `Play ACF Camera Effect` (Preset, Params) → Handle | Params: Scale、発生源（位置・Actor）、Target Actor（Speed FOV / Roll 用、空ならプレイヤーの Pawn）、Instigator（破棄されたら停止）、Direction、Random Seed |
| `Stop ACF Camera Effect` (Handle, Immediate) | その演出だけを Release（Immediate なら即座に消す） |
| `Stop All ACF Camera Effects` | すべて停止 |
| `Play ACF Camera Impulse` (Source Location, Scale, Preset) | Preset を省略すると、Project Settings の既定 → 組み込みの既定の順に使います |
| `Play ACF Camera Recoil` (Scale, Preset) | 同上 |
| `Play ACF Camera Shake` (Shake Class, Scale) | UE の Shake クラスを ACF 経由で再生します。PCM には登録しないので、二重にはかかりません |
| `Get Active ACF Camera Effects` / `Is ACF Camera Effect Active` | 再生中の演出の一覧（Handle、Preset、経過時間、Weight、停止中かどうか） |

演出の状態は再生ごとに確保され、終了・停止・Instigator の破棄と同時に解放されます（Shake のインスタンスも GC で回収されることを確認済み）。

## 7. 他のシステムとの関係

**適用順序**（1 フレーム、PCM の Modifier は Priority の小さい順に適用されます）

```
ViewTarget の CameraComponent / SpringArm         → UE カメラ（Upstream）
[0〜63]   他の CameraModifier                     ※ACF の Weight = 1 の間は上書きされます
[64]      ACF Output    Rig → ブレンド → 位置の演出 → Collision → 回転・FOV の演出 → Lerp(UE カメラ, ACF, Weight)
[127]     UE Camera Shake（その他 65〜254 の Modifier）
[255]     ACF Guard     Collision の拘束で最終位置を再 Sweep（Weight 合成や Shake での壁抜けを防ぐ）
→ PCM の ViewTarget ブレンド → 描画
```

- **Camera Shake**: そのまま使えます。壁際で Shake がカメラを壁へ押しても、Guard が手前で止めます。
- **Guard の注意**: PCM の `SetViewTarget(..., BlendTime)` によるブレンドは Guard の後に PCM が行うので、Guard では防げません。ACF の使用中は PCM の ViewTarget ブレンドを使わないでください。
- **Sequencer など**: 原則として ACF の出力を抑えます。自動検出はまだないので、`Suppress Output (Source=Self)` と `Release Output Suppression` を自分で呼んでください。抑制中も ACF は内部で評価を続けます。
- **照準と見た目**:
  - `Get Aim Camera Pose`
    - 値: `Lerp(UE カメラ, ACF(位置の演出 + Collision 後), Weight)`
    - 含まないもの: 回転・FOV の演出、UE Shake、Guard の補正、PCM の ViewTarget ブレンド
    - 用途: 照準・トレース（Shake で揺れない）
    - 注意: UE カメラとの Weight ブレンド中は、位置が障害物の中を指すことがあります。
  - `Get Visual Camera Pose`: 実際に描画されるカメラ（PCM の CameraCache）です。
- **ネットワーク**: ACF はカメラ情報をサーバーへ送りません。

## 8. 設定・デバッグ

- **Project Settings > Plugins > Action Camera Framework**
  - Output Modifier Class
  - Enable Output Collision Guard（既定 ON）
  - Log Requests
- **`ACF.Debug.Draw 1`**: ワールドに描画します。
  - 追従対象（緑）、Lookahead の予測位置（水色）、注視点（黄）
  - Collision の Sweep（ヒット時は赤）、カメラ
  - Group Framing の対象の球と重み付き中心（マゼンタ）
  - Lock-On のロック点（赤。対象喪失中は灰色）、プレイヤーとの線、注視点（オレンジ）
- **`ACF.Debug.Draw 2`**: 上の描画に加えて、画面に表示します。
  - Dead Zone（緑枠）、Soft Zone（赤枠）、画面上の対象位置、Framing Size（マゼンタ枠）
  - Group の対象数・前後移動量・FOV 補正、Lock-On の距離と対象喪失の表示
  - カメラ位置・回転、Orbit の Yaw / Pitch / 距離、Collision の状態
- **`showdebug camera`**: ACF の要求、Weight、抑制状態を表示します。
- **コンソール**
  - `ACF.Debug.PushFixed` / `ReleaseLast` / `ReleaseAll` / `Dump`
  - `ACF.Sample.*`（9 章）
- デバッグ処理とサンプル用コマンドは、Shipping ビルドではコンパイル自体から除外されます。

## 9. 手動確認（Editor で実際に見る）

自動テストは NullRHI（描画なし）で実行しています。描画結果と操作感は、次の手順で人が確認してください。

**準備**
1. Content Browser で Settings > Show Plugin Content を ON にします。
2. `/ActionCameraFramework/Samples/L_ACFCameraTest` を開いて PIE（Play）します。
   - Pawn と入力はプロジェクト既定の GameMode のものです。REBoarGetch 側の変更はありません。
3. `~` でコンソールを開き、`ACF.Debug.Draw 2` を実行します。
   - このプロジェクトでは RetroRender の PS1 / CRT ルックが全ゲームワールドに自動でかかります。カメラの動きを素の画面で見たい場合は `Retro.Preset OFF` を実行します（ACF とは無関係）。

**操作**
- マウス: プロジェクト既定の Look 入力（ControlRotation）でカメラを旋回します。
- ゲームパッド: 右スティックで旋回します。
- サンプル Preset はすべて Input Source = Control Rotation なので、既存の入力設定のまま動きます。
- ACF の `AddCameraInput` / `SetCameraInputRate` 経由の入力は、`BP_ACFOrbitInputTest` と自動テストで確認しています。

| 確認項目 | 手順 | 期待する見た目 |
|----------|------|----------------|
| Follow | `ACF.Sample.Use Follow` で歩き回り、急に止まる | 進行方向へ少し先行し（Lookahead）、止まると滑らかに背後へ戻る |
| Orbital Follow | `ACF.Sample.Use ThirdPerson` でマウス・スティックを回す | 肩越しの TPS カメラ。入力に遅れなく回る |
| Pitch 制限 / FreeLook | `ACF.Sample.Use FreeLook` で上下いっぱいに振る | 上を向くと遠く高く、下から見上げると近くなる（3 リング）。-70° / +50° で止まる |
| Dead / Soft Zone | `ACF.Sample.Use Composer` で LookTarget の柱の周りを歩く | 柱が画面右寄りの緑枠に収まる。緑枠の中では回らず、赤枠の外へは出ない |
| Collision（壁） | ThirdPerson で `Wall` に背を向けて近づき、カメラを壁側へ回す | カメラが壁の手前で止まり、壁の中が見えない。離れると滑らかに戻る |
| Collision（狭い通路・天井） | `Corridor` に入り、カメラを左右・上へ回す | 壁と天井を貫通しない。出口で滑らかに戻る |
| Camera Blend | `ACF.Sample.Use FreeLook 2` → `ACF.Sample.Use Follow 2`（秒数を変える。3 番目の引数 `0` で Snapshot） | 2 秒かけて切り替わる。Live では切替元も動き続け、Snapshot では止まる |
| UE カメラへの戻り | `ACF.Sample.Release 1` | 1 秒で UE 既定のカメラへ戻る |
| Shake と Collision | 壁際または通路で `ACF.Sample.Shake`、比較として `ACF.Sample.Guard 0` → `ACF.Sample.Shake` | Guard ON では壁の中が見えない。OFF では壁の中が一瞬見える（既知の挙動の再現） |
| Weight ブレンドと柱 | `Pillars` の間で `ACF.Sample.Release 1` / `ACF.Sample.Use ThirdPerson 1` を繰り返す | ブレンド中に柱の中が見えない |
| テレポート | `ACF.Sample.Teleport 3000` | カメラが飛んで行かず、新しい位置でカットされる |
| Recoil / Punch | `ACF.Sample.Effect Recoil`（連打する）、`ACF.Sample.Effect Punch` | 反動が重なり、短く戻る。Pitch の反動で照準（Aim）は動かない |
| Impulse | 壁際で `ACF.Sample.Effect Impulse 3` | 揺れても壁を抜けない（Guard を `ACF.Sample.Guard 0` にしても同じ） |
| ACF Shake | 通路で `ACF.Sample.Effect Shake` | UE の Shake がかかるが、壁・天井を抜けない。2 重にかからない |
| Speed FOV / Roll | `ACF.Sample.Effect Motion`（もう一度で OFF）にしてダッシュ・横移動・旋回する | 速いほど FOV が広がり、旋回や横移動で傾く。止まると滑らかに戻る |

`ACF.Sample.Use <Follow|ThirdPerson|FreeLook|Composer> [BlendSeconds=0.6] [Live=1]` は、プレイヤー Pawn を追従対象、`ACFLookTarget` タグの Actor を注視対象として要求を出し直します。

**Lock-On / Target Group（Phase 4A）**

テストマップには `ACFEnemy` タグの円柱が 4 本あります: `Enemy_0_Near`（スタート地点の近く）、`Enemy_1_Far`（遠く）、`Enemy_2_Boss`（高さ 6m、壁の向こう）、`Enemy_3_Small`（低い）。
先に `ACF.Sample.Use ThirdPerson` で通常カメラを出しておくと、解除したときにそのカメラへ戻ります。

| 確認項目 | 手順 | 期待する見た目 |
|----------|------|----------------|
| ロック開始 | `ACF.Sample.LockOn 0` | 0.5 秒で回り込み、プレイヤーの肩越しに Enemy_0 を正面に捉える。プレイヤーと敵の両方が画面に入る（マゼンタ枠の中） |
| 相対移動 | ロック中に敵の周りを歩く・走る。`ACF.Sample.Enemy Circle 90`（もう一度で停止）で敵がプレイヤーの周りを回る | 常に敵の反対側に回り込み、両方が画面から出ない。マウスでは回らない |
| 近距離・遠距離 | Enemy_0 に密着するまで近づく → `ACF.Sample.LockOn 1`（遠くの敵） | 近いほどカメラが引いて FOV 90°、遠いと寄って FOV 70°。重なっても回転が暴れない |
| ターゲット切替 | `ACF.Sample.LockOn 2`（ボス）→ `ACF.Sample.LockOn 3` | カットではなく滑らかに回り込む。ボスでは下から見上げる |
| 急移動・テレポート | ロック中に `ACF.Sample.Enemy Teleport 1500`、`ACF.Sample.Teleport 3000`（プレイヤー） | 敵の瞬間移動は滑らかに追う。プレイヤーのテレポートはカット |
| 対象の破棄 | `ACF.Sample.Enemy Kill` | ロックオンが自動で解除され、通常カメラへ戻る（ACF.Debug.Draw 2 で要求数が 1 に） |
| ロック解除 | `ACF.Sample.LockOff` | 0.5 秒で通常カメラへ戻る。ロック中の向きのまま戻る（サンプルが ControlRotation を合わせる） |
| Collision | ロックしたまま `Wall` や `Corridor` に背を向けて下がる | カメラが壁・天井を抜けない |
| Effects | ロック中に `ACF.Sample.Effect Recoil` / `Punch` / `Impulse` / `Shake` | 演出がロックオンのカメラの上にかかり、壁を抜けない |
| 出力抑制 | （コンソールコマンドはありません）BP から `Suppress Output` / `Release Output Suppression` を呼ぶ | 抑制中は UE カメラ。解除すると最新の構図で即座に復帰する（自動テストで確認済み） |
| Target Group | `ACF.Sample.Group Use` → `ACF.Sample.Group Add 0` → `Add 2` → `Add 1` | メンバーを足すたびに必要な分だけ引いて（足りなければ FOV を広げて）、全員がマゼンタ枠に入る |
| Weight / Radius | `ACF.Sample.Group Weight 2 0.2`、`ACF.Sample.Group Radius 2 600` | Weight を下げると中心がボスから離れて寄る。Radius を上げるとさらに引く |
| メンバー削除 | `ACF.Sample.Group Remove 2`、`ACF.Sample.Group Release` | ゆっくり寄って戻る（ポップしない）/ 通常カメラへ戻る |

コマンド:
- `ACF.Sample.LockOn [Index]`: `ACFEnemy` をラベル順に番号付け。ロック中なら対象の切替（省略時は次の敵）
- `ACF.Sample.LockOff [BlendSeconds=0.5]`
- `ACF.Sample.Enemy <Circle [DegPerSec=60]|Teleport [Distance=1500]|Kill>`: ロック中の敵を操作（Kill はその PIE セッション中は戻りません）
- `ACF.Sample.Group <Use|Add i|Remove i|Weight i w|Radius i r|Release>`: プレイヤー（Weight 1、Radius 60）を含むグループ。追加する敵の Radius は 80

## 10. 拡張（プログラマー向け）

`UACFCameraNode` を継承し、`GetStage()`（Body / Aim / Framing / PostBlend）と `Evaluate` を実装します。

- Node のメンバーは共有設定です。実行時に書き換えないでください。
- フレームごとに変わる値は USTRUCT に入れ、`GetRuntimeStateType()` で返します。
- テレポートには `ResetInterpolation`、有効化には `OnActivated` で対応します。
- `DeltaTime <= 0` のときは状態を進めないでください。
- Body の Node は `Context.Scratch.Pivot` を設定してください（Collision の Sweep 起点になります）。
- Framing 段の Node は位置（視線方向）と FOV だけを変え、回転は変えないでください。
- 共通部品: `FACFCameraTargetTrackingState::Track` / `TrackPoint`（対象喪失・テレポート・Lookahead）、`UpdateDamped`（軸別 Damping）、`ACFFraming`（画面への投影・構図）、`ACFTargetGroup::GatherSubjects`（Target Group の解決）、`UACFCameraNode_GroupFraming::ComputeRequiredDolly / ComputeRequiredFov`、`ACFCameraCollision::SweepCamera`
- 新しい演出: `UACFCameraEffect` を継承し、`Evaluate` で `FACFCameraEffectOutput`（チャンネル・値・空間）を返します。状態は `GetRuntimeStateType`、後始末は `OnEnd` で行います。Envelope・Weight・減衰・合成はスタック側が行います。

## 11. 制限（Phase 4A 時点）

**Lock-On / Target Group**
- 視線の遮蔽は扱いません。ボスが壁の向こうにいると、構図には入っていても壁に隠れます（Deoccluder は未実装）。
- Collision がカメラを手前に押すと、Group Framing が引いた分が失われ、対象が枠からはみ出すことがあります（壁抜けを防ぐ方を優先）。Mode を Distance Then Field Of View にすると、Max Dolly Out を超えた分だけは FOV で補いますが、Collision で縮んだ分は補いません。
- ロックオン中の移動入力の基準（ControlRotation）は変更しません。解除時にカメラの向きへ合わせるかどうかはゲーム側で決めます（サンプルは合わせます）。
- ロックオンの解除は戻り先カメラの Blend In を使います。解除専用のブレンドを指定する API はありません。
- Group Framing はメンバーの位置を平滑化しません（Lock-On のロック点だけは平滑化されたものを使います）。急な追加は Widen Damping Time で滑らかになりますが、メンバー自身の瞬間移動は枠の調整が追いかける形になります。
- Weight の小さいメンバーは、`Weight / 最大 Weight` の割合で枠に入れる扱いになるので、完全には収まりません（仕様）。

**全般**

- 描画結果の目視確認と、実機のマウス・パッドでの操作感確認は、まだ行っていません（9 章の手順で確認してください）。
- PCM の ViewTarget ブレンド中の壁抜けは防げません（7 章）。
- ブレンド中に割り込むと、割り込まれた側の動きはそれ以降反映されません。
- Collision は「ピボット → カメラ」の線上だけを見ます。視線上の遮蔽物を避ける Deoccluder は Phase 4B 以降です。
- SpringArm と一致することは、静止時（Lag 収束後）と障害物ありで確認しました。移動中の過渡応答（Lag の式）は異なります。
- Sequencer の自動検出、Blueprint 製の Node / Effect は未対応です。
- 演出は ACF カメラにかかります。UE カメラだけの場面では、素通し Preset を要求してください（6 章）。
- Shake の PostProcess 出力は PCM に渡します。ただし PCM が作り直されると、それ以降の PostProcess は失われます（位置・回転・FOV は影響を受けない）。
- 演出のステージは 2 つ（Collision の前と後）で固定です。演出ごとに段を選ぶことはできません（位置は前、回転・FOV は後）。

## 12. 検証

- 検証用アセットは、どちらも作り直せます（既存のアセットは再利用し、グラフだけ書き直します）。
  - `Content/Verification/`（DA ×3、BP ×2）: `Scripts/CreateVerificationAssets.py`
  - `Content/Samples/`（DA ×6、テストマップ）: `Scripts/CreateManualTestAssets.py`
- 自動テストは 63 件です。
  - `.Director.*` / `.Nodes.*` / `.ThirdPerson.*` / `.Effects.*` / `.Action.*`（42 件）: Editor とゲームの両方で実行できます。
  - `.Integration.*`（18 件）: `-game` でのみ実行されます。
  - `.Samples.*`（3 件: TestMapSmoke、TestMapScreenshot、LockOnAndGroupOnTestMap）: テストマップ上でだけ実行されます。他のマップではスキップします。スクリーンショットは描画あり（`-NullRHI` なし）のときだけ `Saved/Screenshots/` に保存されます。
- Phase 4A で追加したテスト（`.Action.*` 10 件、`.Integration.LockOnCollisionEffectsAndDestroy`、`.Samples.LockOnAndGroupOnTestMap`）の確認内容:
  - ロック開始・解除（ブレンド中に 1 フレーム 40cm 超の飛びがない）、敵が周回・プレイヤーが移動しても両方が画面内
  - 近距離・遠距離のアーム長と FOV、重なり・真上でも NaN にならず Yaw を保持
  - 敵の瞬間移動・ターゲット切替は滑らか、プレイヤーのテレポートはカット、破棄で保持または自動解除（切替後の旧対象の破棄では解除しない）
  - ブレンド中のターゲット切替、高優先度カメラへの切替と復帰、出力抑制中も評価が続き復帰時に最新、同一フレームの二重評価で状態が進まない
  - Group の追加・削除・破棄メンバーの自動除外、Weight / Radius による変化、0 件・1 件、FOV モード、Fill、Distance Then FOV、必要距離・必要 FOV の解析解との一致
  - 実際の PCM 上で Collision（Guard OFF）と Effects の併用、テストマップ上でのコンソールコマンド一式

```bash
UnrealEditor-Cmd.exe <project>.uproject -unattended -NullRHI -nosplash "-ExecCmds=Automation RunTests ActionCameraFramework.Director+ActionCameraFramework.Nodes+ActionCameraFramework.ThirdPerson" "-TestExit=Automation Test Queue Empty"
```

```bash
MSYS_NO_PATHCONV=1 UnrealEditor-Cmd.exe <project>.uproject /Game/Level/L_Lobby -game -unattended -NullRHI -nosplash -nosound "-ExecCmds=Automation RunTests ActionCameraFramework" "-TestExit=Automation Test Queue Empty"
```

```bash
MSYS_NO_PATHCONV=1 UnrealEditor-Cmd.exe <project>.uproject /ActionCameraFramework/Samples/L_ACFCameraTest -game -unattended -NullRHI -nosplash -nosound "-ExecCmds=Automation RunTests ActionCameraFramework.Samples" "-TestExit=Automation Test Queue Empty"
```

## 13. ファイル構成

| パス | 内容 |
|------|------|
| `Source/ActionCameraFramework/Public/` | `ACFCameraSubsystem`（入口）、`ACFCameraDirector`、`ACFCameraPreset`、`ACFCameraNode`（段・Scratch）、`ACFCameraRigInstance`、`ACFCameraOutputModifier`（Output と Guard）、`ACFCameraTypes`、`ACFCameraSettings`、`ACFCameraBlueprintLibrary` |
| `Source/ActionCameraFramework/Public/Nodes/` | FixedPose / Follow / LookAt / OrbitalFollow / Composers / LockOn / GroupFraming / Collision と、共通部品の TargetTracking（Damping・Lookahead・テレポート）/ Framing（画面座標の計算）/ TargetGroup（グループの解決） |
| `Source/ActionCameraFramework/Public/Effects/` | `ACFCameraEffect`（Layer 基底・Effect Preset）、`ACFCameraBuiltinEffects`（Punch / Recoil / Impulse / Shake / Speed FOV / Roll）、`ACFCameraEffectStack`（Instance・Envelope・合成）、`ACFCameraEffectTypes` |
| `Source/ActionCameraFramework/Private/` | 上記の実装、`ACFCameraDebugDraw`、`ACFCameraDebugCommands`、`ACFCameraDebugShake`（サンプル用 Shake）、`Tests/` |
| `Source/ActionCameraFrameworkEditor/` | Editor モジュール（Phase 6 のツール置き場） |
| `Content/Verification/` | 自動テスト用アセット |
| `Content/Samples/` | 手動確認用のサンプル Preset とテストマップ `L_ACFCameraTest` |
| `Scripts/` | 上記アセットの生成スクリプト |
| `Docs/`（Git 管理外） | 引き継ぎ用の詳細資料（設計判断、監査結果、Effect Pipeline 設計） |
