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
- Phase 4B（Combat Camera、Deoccluder、カメラ制約の調停 = Constraint Solver）完了
- Phase 4C（Finisher Camera、Cinematic Camera Sequence、Shot Anchor、アニメーション同期用 Notify）完了
- Phase 5A（Level Sequencer との連携: Camera Cut 中の出力制御、復帰ブレンド、外部 ViewTarget）完了。自動テストと描画ありのテストマップで確認済み（14 章）。Editor での人による操作感の確認は未実施
- Phase 6A（品質・アーキテクチャ監査）完了。確認済みの不具合は 0 件、設計リスク 9 件・改善候補 25 件を記録（`Docs/Audit_Phase6A.md`）。設計リスクは再現しておらず、性能は未計測
- Phase 6B（監査結果の修正）を実施中。B-1（計測の土台と性能の実測）〜 B-9（テストの土台、22 章）が完了し、次は B-10。今後の計画は下の「開発ロードマップ」

**開発ロードマップ**（詳細は `Docs/Roadmap.md`: 各 Phase の目的・予定機能・設計上の注意点・依存・完了条件。`Docs/` は Git 管理外です）

| Phase | 内容 | ステータス |
|-------|------|-----------|
| 1〜5A | 上記 | Completed（Editor で人が操作して見る確認は全 Phase で未実施） |
| 6A | 品質・アーキテクチャ監査（問題一覧・根拠・優先度・修正計画） | Completed（`Docs/Audit_Phase6A.md`） |
| 6B | 監査結果の修正（11 タスク。計測の土台と PCM の互換性を最優先、再現テストと回帰テスト） | In Progress（B-1〜B-9: Completed。次は B-10） |
| 7A | Runtime Camera Tuning + Camera Profiler / Debugger（共有 Preset は Runtime から直接変更しない。構図ガイドを含む） | Not Started |
| 7B | Camera Input Assistance（入力の取得はゲーム側、ACF は入力値の処理とカメラ制御） | Not Started |
| 7C | Camera Volume / Zone（レベル用 Actor と Core の依存を分ける。候補: レベル配置用 Camera Actor、Confiner） | Not Started |
| 7D | Advanced Occlusion Handling（複数点の遮蔽判定、可視率、一時透過。Trace 数を計測） | Not Started |
| 7E | Camera Recorder / Replay（カメラ出力の再生。ゲームプレイの完全再現とは区別） | Not Started |
| 7F | Spline / Rail Camera（Sequencer と重複する機能は実装しない） | Not Started |
| 8 | 独立プラグインとしての完成検証（新規プロジェクトへの導入、パッケージビルド、手動操作テスト） | Not Started |

Phase 6B の範囲と順序は、Phase 6A の監査結果で確定しています。Phase 7 以降の設計は、各 Phase の開始時に見直します。

**決定済みの方針**（Phase 6A）
- 対応する PlayerCameraManager: 標準の `APlayerCameraManager` と、その派生クラスです。POV をコピーして Modifier に渡す派生クラス（GameplayCameras の PCM など）は条件つきで対応します。条件・導入方法・診断は 15 章を参照してください。
- モジュール: Core / Sequencer / Editor / Tests / Samples の責務分離を目標にします。Sequencer の別プラグイン化は Phase 8 で再評価します。
- 保留: State-Driven Camera、Lens / Post Process、Editor Preview、専用の Preset 編集 UI。

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
| 2a. シーケンス | 再生中の Camera Sequence（Finisher）の時間を進め、Shot が変わるなら Shot の要求を差し替えます（同じフレームの 2 で反映） |
| 2. 要求の解決 | 優先度で有効な要求を決め、必要なら遷移（ブレンド）を開始します |
| 3. Rig 評価 | **Body**（位置）→ **Aim**（回転）→ **Framing**（視線方向の移動・FOV で対象を画面に収める）の順に Node を評価します |
| 4. ブレンド | 要求同士のブレンド（Live / Snapshot）と、UE カメラに対する Weight |
| 5. 演出と PostBlend | 位置の演出 → **Collision**（ブレンド後の最終姿勢に 1 回。中で Constraint Solver が「遮蔽補正 → 衝突 → FOV 回復」の順に動く）→ 回転・FOV の演出。ブレンド中や演出中でも壁を抜けません |
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
| **Shot Anchor** | Body | 演出用。攻撃者（Follow Target）と対象（Look At Target）を結ぶ線を基準にカメラを置きます。毎フレーム追従（動的ショット）か、Shot 開始時に固定（固定ショット）。位置 + 回転 + FOV |
| **Look At** | Aim | 対象を真っ直ぐ注視します |
| **Rotation Composer** | Aim | 対象が画面上の決まった位置に来るよう、カメラを**回転**します（Dead / Soft Zone あり） |
| **Combat Camera** | Framing（段の先頭） | ゲームから渡された戦闘状態に応じて、アーム長（敵との距離で変化）・FOV・肩越しオフセットを滑らかに変えます。回転はしません |
| **Group Framing** | Framing | 複数の対象（球）が画面の指定範囲に収まるよう、カメラを**視線方向に前後**させるか **FOV** を変えます。回転はしません |
| **Collision** | PostBlend | ピボットからカメラへ Sphere Sweep し、壁へのめり込みを防ぎます。**Constraint Solver**（Deoccluder / Framing Recovery、既定 OFF）もここに設定します |

Look At と Rotation Composer の **Target Source** は、Look At Target / Follow Target / **Target Group**（グループの重み付き中心）から選べます。

**よく使う組み合わせ**
- TPS / アクション: `Orbital Follow` + `Collision`（必要なら `Rotation Composer` で画面上の位置を調整）
- 追従カメラ: `Follow` + `Look At`
- 2.5D / 横スクロール: `Fixed Pose`（回転だけ ON）+ `Position Composer`
- ロックオン: `Lock-On` + `Group Framing`（Follow / Look At Target Subject を ON）+ `Collision`（サンプル `DA_ACFSample_LockOn`）
- 複数対象: `Orbital Follow` + `Rotation Composer`（Target Source = Target Group）+ `Group Framing` + `Collision`（サンプル `DA_ACFSample_Group`）
- 戦闘: 上の TPS / ロックオンに `Combat Camera` を足し、`Collision` の Deoccluder と Framing Recovery を ON（サンプル `DA_ACFSample_ThirdPerson` / `DA_ACFSample_LockOn`）
- フィニッシャーの Shot: `Shot Anchor` + `Collision`（サンプル `DA_ACFSample_Shot_*`）。Shot を並べる Camera Sequence は下の「Camera Sequence / Finisher」

位置を決める Node（Follow / Orbital Follow / Lock-On / Position Composer / Shot Anchor / 位置を上書きする Fixed Pose）は 1 つだけにしてください。保存時の Data Validation が、役割の競合・NaN・範囲外の値などを報告します（21 章）。

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

### Combat Camera の設定

戦闘中かどうか・強さ（Intensity 0〜1）・距離の基準にする敵は**ゲームが決めて**渡します（`Set ACF Camera Combat State`。ローカルプレイヤーごと）。ACF は戦闘を判定しません。
同じ Node を通常の TPS Preset とロックオン Preset の両方に入れれば、通常戦闘・ロックオン戦闘の両方に効きます。

| 項目 | 意味 |
|------|------|
| Profile: Near / Far Range | プレイヤーと敵の距離。この間で Near → Far の値を線形補間します（敵がいなければ Far） |
| Profile: Near / Far Distance Scale | アーム長（ピボット → カメラ）の倍率。既定は近い敵ほど引きます（1.25 / 1.05） |
| Profile: Field Of View Offset | FOV に足す角度（既定 +5°） |
| Profile: Shoulder Offset | カメラ空間で足す肩越しオフセット（Y = 右、Z = 上） |
| Enter / Exit Time | 戦闘に入る・出るときの補間の時間（既定 0.35 / 0.8 秒） |
| Range Damping Time | 敵との距離の変化の平滑化 |

- 距離の基準の敵: Combat State の Focus Target → ロックオンの対象（Look At Target）→ Target Group で一番近いメンバーの順に使います。
- 変化量はすべて `Intensity` 倍されます。`bOverrideProfile` でゲームから Profile ごと差し替えることもできます。
- ピボットを中心にアームを伸ばすので、プレイヤーの画面上の位置は保たれます。後段の Group Framing が対象の収まりを、Collision が壁を保証します。

### Constraint Solver（Collision Node の Deoccluder / Framing Recovery）

Collision Node が 1 フレームに 1 回、次の順で処理します。どの制約も同時に満たせないときは、下の優先順位で段階的に緩めます。

| 優先 | 制約 | 担当 |
|------|------|------|
| 1 | カメラ自身が障害物に入らない | Collision の Sweep（**常に最後に勝つ**。Guard も維持） |
| 2 | プレイヤーが見える | Deoccluder（プレイヤーを最優先で確認）、最後の手段として Framing Recovery がプレイヤー方向へ少し回す |
| 3 | 重要な敵ができるだけ見える | Deoccluder（Weight の合計が大きい候補）、Framing Recovery（FOV を上限まで広げる） |
| 4 | 元の構図・距離を保つ | 補正は「必要なときだけ」「上限つき」。障害物が消えたら元へ戻る |
| 5 | 移動量・回転量を最小に | 候補を移動量の小さい順に試し、全員見える最初の候補で止める |

**Deoccluder**（Deoccluder > Enabled）
- 確認する対象: Group Framing の対象（プレイヤー → Weight 順、最大 Max Checked Subjects 体）。Group Framing が無い Preset では、ピボット（プレイヤー）とロックオン点 / Look At Target。
- 視線の確認は、Collision 後のカメラから各対象への Sphere Trace（Trace Radius、0 なら Line Trace）です。プレイヤー・操作中の Pawn・Ignored Actors は無視し、対象自身に先に当たった場合は「見えている」とします。
- **ヒステリシス**: Occluded Delay（既定 0.25 秒）以上続いた遮蔽にだけ反応します。柱の前を一瞬横切るだけなら動きません。補正を戻すのは、元の位置から全員が Clear Delay（既定 0.5 秒）以上見えてからです。
- **補正**: ピボットを中心にカメラを回す（Yaw ±、Pitch 上向き）か、手前に寄せます（Pull In）。候補は ±Max Yaw の半分 / 上へ Max Pitch の半分 / 寄せ半分 → 組み合わせ → 最大値の順（最大 10 候補）。各候補は Collision の Sweep 後の位置で評価します。視界の向きも同じだけ回るので、プレイヤーの画面上の位置は変わりません。
- **補正量の上限**: Max Yaw Offset（既定 50°）、Max Pitch Offset（25°）、Max Pull In（アームの 60%）。
- **滑らかさ**: 補正へは Correction Damping Time（0.3 秒）、戻りは Return Damping Time（0.7 秒）。
- **解決できないとき**: 最もよく見える候補（プレイヤー優先、次に敵の Weight の合計）に留まり、見えない対象の数を `Get Last Evaluation Result` の `NumOccludedTargets`（プレイヤー自身なら `bPlayerOccluded`）で知らせます。毎フレームは探さず、Retry Interval（0.3 秒）ごとにだけ探し直します。上限を超えて動いたり、カメラが障害物に入ったりはしません。ゲーム側でシルエット表示などを出す判断に使ってください。
- **トレース数**: 通常 1 フレームに「Sweep 1 回 + 対象数」。補正中は元の位置の確認でもう 1 組。探索するフレームだけ最大 10 候補 ×（1 + 対象数）。
- 要求ごとに `Set ACF Camera Deoccluder Enabled` で OFF にできます。

**Framing Recovery**（Framing Recovery > Enabled。Group Framing がある Preset で働きます）
- Collision がカメラを手前に押した後、Group Framing の対象が枠（Framing Size）からはみ出すなら、必要な FOV を計算して広げます（Phase 4A の「Group Framing と Collision の競合」の解決）。
- 上限: Max Field Of View（既定 100°）と、元の FOV + Max Field Of View Increase（既定 +25°）の小さい方。上限で足りない場合は `bFramingLimited` を立て、敵が枠から出ることを許します（優先 3 を緩める）。
- それでもプレイヤーの中心が枠の外なら、プレイヤーの方へ最大 Max Player Rotation（20°）だけ回します（優先 2 を守る）。
- 広げるときは速く（0.1 秒）、戻すときはゆっくり（0.6 秒）。対象が収まっているときは何もしません（Group Framing と競合しません）。

### Shot Anchor の設定（Finisher / Cinematic の Shot 用）

| 項目 | 意味 |
|------|------|
| Anchor Point / Between Alpha | 基準点: Follow Target（攻撃者）/ Look At Target（対象）/ その間（0 = 攻撃者、1 = 対象）。片方が無ければもう片方 |
| Anchor Offset | 基準点に足すワールドオフセット（例: 胸の高さ） |
| Basis | カメラオフセットの軸: World / 攻撃者の向き / **Line Of Action**（既定。X = 攻撃者 → 対象の水平方向、Y = その右、Z = 上） |
| Camera Offset | 基準点からのカメラ位置（Basis の軸で） |
| Freeze At Shot Start | ON = **固定ショット**（Shot の開始時に基準点と軸を記録し、以後動かない）。OFF = **動的ショット**（毎フレーム追従） |
| Damping Time | 動的ショットの追従の遅れ |
| Aim At Anchor / Field Of View | 基準点を注視するか（OFF なら Look At などの Aim Node に任せる）/ FOV |

- 基準点を Collision のピボットとして渡します。Shot のカメラが壁の向こうに来ても、Collision は「演出の中心 → カメラ」の線上で手前に寄せます。
- 攻撃者・対象が破棄されたときは、最後の基準点に留まります。
- Line Of Action を使うと、レベルのどこで演出が起きても「攻撃の右側から」のような同じ構図になります。

### Camera Sequence / Finisher（Phase 4C）

Data Asset > **ACFCameraSequence** を作り、`Shots` に Shot を並べます。Sequence も Preset と同じく共有・不変で、再生ごとの状態は再生（Handle）ごとに持ちます。

| Shot の項目 | 意味 |
|-------------|------|
| Name | Jump To Shot・通知・デバッグ表示で使う名前 |
| Preset | その Shot のカメラ（動的 / 固定は Preset の Shot Anchor で決まる。Lock-On や Orbital Follow の Preset も使える） |
| Advance Mode / Duration | **Duration**: Duration 秒（シーケンス時間）で次へ。**Wait For Event**: Advance イベントを待つ。Duration は**安全のためのタイムアウト**（0 = 無限）。どちらのモードでも Advance イベントで早めに進められます |
| Blend In | この Shot へ入るときの切替。Cut または Blend（最初の Shot は、直前のゲームカメラからの切替） |
| Priority Offset | Sequence の Priority に足す値 |
| Targets | Follow / Look At に使う役割（Attacker / Target / None）と Socket、Target Group に入れる重み（Group Framing を使う Preset 用） |
| Enable Deoccluder | この Shot で Collision Node の Deoccluder を使うか（既定 OFF = 演出の構図を崩さない） |
| Fallback | 狭い場所で Shot が成立しないときの代わり（下の「Collision と演出」） |
| Effects | Shot 開始時に再生する Effect Preset。寿命（Lifetime）: **Shot**（Shot の終わりで Release）/ **Sequence**（再生の終わりで Release）/ **Fire And Forget**（自然に終わる。Cancel では即座に止める） |

| Sequence の項目 | 意味 |
|-----------------|------|
| Priority | Shot の要求の Priority（既定 1000。ロックオンなどゲームのカメラより上） |
| Return Blend / Cancel Blend | 終了（最後の Shot の後・Finish）/ Cancel で、**どのカメラへ戻る場合でも**使うブレンド（既定 0.6 秒 / Cut） |
| Interrupt Policy | もっと高い Priority のカメラに割り込まれたとき: **Continue**（既定。時間は進み続け、割り込みが終われば続きを映す）/ **Pause**（時間を止める）/ **Cancel**（終了） |
| Actor Lost Policy | 攻撃者・対象が破棄されたとき: **Finish**（既定。Return Blend で戻る）/ **Cancel** / **Continue**（Node は最後の位置を保持） |
| Use Attacker Time Dilation | シーケンス時間に攻撃者の Custom Time Dilation を掛ける（既定 ON。攻撃者のヒットストップでカメラのタイミングも止まる） |
| Collision Ignores Role Actors | 攻撃者・対象を Shot の Collision の無視リストに入れる（既定 ON） |

Data Validation は、Preset の無い Shot（エラー）、Collision の無い Preset、タイムアウトの無い Wait For Event、重複した Shot 名（警告）を報告します。ブレンドでつながる Shot の間で Collision の有無や設定が変わるときは、その注意も出します（Info、21 章）。

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

**戦闘・制約**（戦闘の判定はゲーム側の責務です）

| ノード | 内容 |
|--------|------|
| `Enter ACF Camera Combat` (Intensity, Focus Enemy) / `Exit ACF Camera Combat` | 戦闘に入る・出る（Combat Camera Node が滑らかに切り替える） |
| `Set ACF Camera Combat State` (State) / `Get ACF Camera Combat State` | 戦闘状態を丸ごと設定（Intensity、Focus Target、Profile の差し替え） |
| `Set ACF Camera Deoccluder Enabled` (Handle, Enabled) | 要求ごとに Deoccluder を ON / OFF |
| `Get Last Evaluation Result` の `NumOccludedTargets` / `bPlayerOccluded` / `bFramingLimited` | 解決できなかった遮蔽・FOV 上限に達したことの通知 |

**Finisher / Camera Sequence**（攻撃・ダメージ・アニメーションはゲーム側の責務です。ACF はカメラだけを扱い、Montage を再生・停止しません）

```
攻撃開始（ゲーム） ─▶ Play Montage（ゲーム）
                  ─▶ Start ACF Finisher Camera (Sequence, Attacker = 自分, Target = 敵, Play Rate = Montage の再生速度) ─▶ Handle を保存
Montage の Notify  ─▶ 「ACF Camera Sequence Command」(Advance Shot / Jump To Shot / Finish / Cancel)   … Handle 不要
Montage の Notify State ─▶ 「ACF Camera Hold Shot」（区間の間、Shot の時間を止める）
攻撃の中断（ゲーム） ─▶ Cancel ACF Camera Sequence (Handle)
```

| ノード | 内容 |
|--------|------|
| `Start ACF Finisher Camera` (Sequence, Attacker, Target, Play Rate) → Handle | フィニッシャー開始。このプレイヤーの他の再生は Replaced で終了します |
| `Start ACF Finisher Camera (Preset)` (Preset, Attacker, Target, Duration, Blend In, Return Blend, Priority) | 専用 Preset 1 つだけのフィニッシャー。Duration 0 = Finish / Cancel まで |
| `Play ACF Camera Sequence` (Sequence, Params) | 汎用（Params: Attacker、Target、Priority の上書き、Play Rate、Instigator、Stop Other Sequences、Start Shot） |
| `Advance ACF Camera Shot` / `Jump To ACF Camera Shot` (Name) / `(Index)` | 次の Shot へ / 指定の Shot へ（途中の Shot はスキップ） |
| `Hold ACF Camera Shot` / `Release ACF Camera Shot` | Shot の時間を止める / 再開（回数を数えます。Hold 2 回なら Release 2 回で再開） |
| `Finish ACF Camera Sequence` / `Cancel ACF Camera Sequence` | 正常終了（Return Blend）/ 中断（Cancel Blend、演出を即座に停止）。Handle は無効化されます |
| `Set ACF Camera Sequence Play Rate` | シーケンス時間の速さ（Montage の再生速度を変えたとき） |
| `Get ACF Camera Sequence State` / `Is ACF Camera Sequence Playing` | 再生中か、現在の Shot（番号・名前・経過時間・長さ）、Hold 中か、イベント待ちか、カメラを握っているか、Fallback 中か、所有している演出の数 |
| `On Camera Sequence Event`（Subsystem のイベント） | Shot Started / Shot Ended / Sequence Ended と理由（Completed / Advanced / Skipped / Infeasible / Cancelled / Interrupted / ActorLost / RequestLost / Replaced） |
| `Send ACF Camera Sequence Command For Actor` (Actor, Command, Shot Name) | Actor が攻撃者か対象になっている再生（全ローカルプレイヤー）に命令を送る。Anim Notify はこれを使います |
| `Start ACF Finisher Camera For Actor` / `Get ACF Camera Subsystem For Actor` | Actor を操作しているローカルプレイヤー（いなければ最初のローカルプレイヤー）で開始 |

**アニメーションとの同期と時間**
- シーケンス時間 = カメラの DeltaTime（ゲーム時間。グローバルの Time Dilation を含む）× Play Rate ×（ON なら）攻撃者の Custom Time Dilation。Montage の再生位置は読みません。
- **Pause**: ワールドが Pause 中は進みません。**Time Dilation**: グローバルは DeltaTime に含まれ、攻撃者のヒットストップは Use Attacker Time Dilation で反映されます。**Montage の再生速度**: Play Rate で合わせます。
- 推奨は「**Shot の切替をアニメーションの Notify（イベント）で行い、Duration はタイムアウトにする**」形です。こうすると Montage の速度変更・途中スキップ・ヒットストップ・フレーム落ちがあっても、カメラの切替は常にアニメーションの該当フレームで起きます。Duration だけで進める Shot は、Play Rate を Montage と揃えてください。
- Advance / Jump は次のカメラ更新で適用されます（Anim Notify はカメラ更新の前に発火するので、同じフレームに反映されます）。同じフレームの Advance は何回送っても 1 回になり、Jump は最後のものが勝ちます。Hold / Release / Finish / Cancel は即座に適用されます。
- 長いフレーム（ヒッチ）で複数の Shot を跨いだ場合は、超過時間を次の Shot へ繰り越し、跨いだ Shot は通知だけ出してカメラは作りません。
- Notify State の Hold は、Montage が中断されても NotifyEnd で解除されます。Notify が来ない場合に備えて、Wait For Event の Shot には Duration（タイムアウト）を入れてください。

**Request / Effect の所有権**
- 1 つの再生は、同時に**最大 1 つ**のカメラ要求（現在の Shot）を持ちます。Shot の切替では「次の Shot の要求を追加 → 前の Shot の要求を削除」の順で行い、前の Shot は Blend の切替元（Live）としてだけ残ります（ブレンドが終わると Director が解放）。
- Shot が再生した演出は再生が所有します。Shot の終わりで Lifetime = Shot の演出を Release、再生の終わりで残りすべてを Release（Cancel では即座に停止）します。終了後に残る演出はありません（Fire And Forget は自分の長さで終わります）。
- 終了のしかた: 最後の Shot の終了・Finish・攻撃者 / 対象の破棄（Finish 設定時）は Return Blend、Cancel は Cancel Blend で、**戻り先がロックオンでも UE カメラでも**そのブレンドを使います。
- 再生の要求が他から消された場合（`Release All Cameras`、Instigator の破棄）は、次の更新で RequestLost として終了し、演出も止めます。

**Collision と演出の優先順位**
1. カメラが障害物に入らない（Shot の Preset の Collision、出力 Guard。Finisher 中も常に有効）
2. Shot の Deoccluder を ON にした Shot では、攻撃者 / 対象が見えるように補正（上限つき）
3. Shot の構図（Shot Anchor の位置）

- 狭い場所: Shot の開始時に 1 回だけ、その Shot のカメラを時間なしで評価（Probe）します。Collision で本来の距離の Min Distance Ratio（既定 60%）未満まで寄せられる、または（Require Line Of Sight）攻撃者 / 対象が遮られている場合は「成立しない」とみなし、**Fallback Preset**（例: 肩越しの近いショット）を使います。Fallback が無く Skip If Infeasible が ON ならその Shot を飛ばし、どちらも無ければ Collision で寄せたまま再生します。
- Shot の途中では切り替えません（Shot の途中でカメラが跳ばない）。途中で狭くなった場合は Collision と（ON なら）Deoccluder が対応します。

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

// Finisher（Montage の再生はゲーム側。カメラ側は Handle で開始・終了する）
FinisherHandle = Cameras->StartFinisherCamera(FinisherSequence, PlayerPawn, Enemy, MontagePlayRate);
Cameras->AdvanceCameraShot(FinisherHandle);                        // 通常は Anim Notify が送る
Cameras->CancelCameraSequence(FinisherHandle);                     // 攻撃が中断されたとき
Cameras->OnCameraSequenceEvent.AddDynamic(this, &AMyCharacter::OnCameraShot);
```

**入力のルール**
- `AddCameraInput` はデルタとして蓄積されます。`SetCameraInputRate` は毎秒あたりの量で、DeltaTime が掛けられます。
- どちらも、次に時間が進むカメラ評価で 1 回だけ消費されます。DeltaTime が 0 の評価や、同じフレームの再呼び出しでは消費されません。
- Rate は消費されると 0 に戻るので、毎フレーム呼び直してください。

## 5. ルール

| 事象 | 挙動 |
|------|------|
| 優先度 | Priority が大きい要求が有効になります。同じ値なら後から出した要求が勝ちます |
| 切替 / 最後の要求を解除 | 新しい要求の BlendIn / 解除した要求の BlendOut を使います（`Release Camera With Return Blend` は戻り先に関係なく指定のブレンド） |
| ブレンド中の割り込み | その時点の画面の姿勢から新しいカメラへブレンドします。画面は飛びません |
| Instigator が破棄 | 次のフレームで自動的に解除されます |
| 追従・注視対象が破棄 | 要求は残り、Lost Behavior に従います。クラッシュはしません |
| 対象を差し替え / テレポート | その Node の補間をリセットします。カメラはレベルを滑らずにカットします（Lock-On のロック対象だけは例外で、切替・瞬間移動とも Lock Target Damping Time で滑らかに回り込みます。プレイヤー側のテレポートはカットします） |
| ロック対象が破棄 | 最後の位置を見続けます。`bReleaseWhenLookAtTargetLost` が ON なら、要求が自動で解除されます |
| ロックオンの解除 | 戻り先のカメラの BlendIn で戻ります（カメラ間の切替なので、解除した要求の BlendOut は UE カメラへ戻るときだけ使われます）。戻り先に関係なく決まったブレンドで戻したい場合は `Release Camera With Return Blend For Player`（20 章） |
| Target Group のメンバーが破棄 | 次の評価でグループから自動的に取り除かれます |
| 遮蔽 | Occluded Delay 以上続いたときだけカメラを動かします。解決できない対象は通知し、カメラは上限内に留まります |
| 戦闘状態の変更 | Combat Camera Node が Enter / Exit Time で補間します。ブレンド中の両方のカメラに同時に効きます |
| Finisher の開始 | 高 Priority の Shot の要求で、ロックオンなど元のカメラの上に乗ります。元の要求は残ります |
| Finisher の終了 | Return Blend（Cancel は Cancel Blend）で元のカメラへ戻ります。戻り先の Blend In ではありません |
| Finisher 中の割り込み | Interrupt Policy に従います（既定は時間を進めたまま裏に回り、割り込みが終わると続きを映す） |
| 攻撃者・対象の破棄 | Actor Lost Policy に従います（既定は Return Blend で終了）。クラッシュはしません |
| PlayerController や PCM の作り直し | ACF が次のティックまでに自動で接続し直します |

## 6. カメラ演出（Effects）

Punch / Recoil / Impulse / Shake / Speed FOV / Roll を、ゲームのイベントから 1 ノードで再生できます。
演出は ACF カメラにかかります。UE 標準のカメラだけを使っている場面で演出を出したい場合は、Node が Collision だけの「素通し Preset」を要求してください。UE カメラがそのまま映り、その上に演出と Collision がかかります。

**ACF カメラが無いときは演出を受け付けません**（Phase 6B / B-5、18 章）。要求が 1 つも無い（または最後の要求がカットで外れた）状態の `Play ACF Camera Effect` などは無効な Handle を返し、演出は保持されません（後から要求を出しても再生されない）。`Can Play ACF Camera Effects` で事前に、`Is Valid (ACF Camera Effect Handle)` で事後に判定できます。

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
- **Sequencer（Level Sequence）**: Camera Cut を自動で検出し、Sequencer のカメラには ACF を乗せません（14 章）。`Suppress Output` を自分で呼ぶ必要はありません。
- **ゲーム独自のカットシーンカメラ**: `Add ACF External View Target (カメラ Actor, Source)` で登録すると、その Actor が ViewTarget の間は ACF を乗せません（14 章）。
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
  - Combat（ON / OFF、重み、アーム倍率、FOV、敵との距離）、Solver（確認した対象数、トレース数、探索したか、補正 Yaw / Pitch / Pull、FOV 回復、LIMIT、見えない対象数）
  - Sequence（Handle、Shot 番号 / 数と名前、経過 / 長さ、Play Rate、wait event、HELD、FALLBACK、not in control、所有する演出の数）
- `ACF.Debug.Draw 1` では Deoccluder の視線も描きます（水色 = 見える、赤 = 遮られている）。
- **`ACF.Debug.Solver 0`**: Constraint Solver（Deoccluder と Framing Recovery）を一時的に無効にします（比較用。Shipping では存在しません）。
  - カメラ位置・回転、Orbit の Yaw / Pitch / 距離、Collision の状態
- **`showdebug camera`**: ACF の要求、Weight、抑制状態を表示します。
- **コンソール**
  - `ACF.Debug.PushFixed` / `ReleaseLast` / `ReleaseAll` / `Dump`
  - `ACF.Sample.*`（9 章）
- デバッグ処理とサンプル用コマンドは、Shipping ビルドではコンパイル自体から除外されます。

### 性能の計測（Phase 6B / B-1）

- **`ACF.Profile 1`**: LocalPlayer ごとに、1 フレームごとの CPU 時間（評価・要求解決・Rig・Node 別・演出・Deoccluder・Guard など）、ACF が発行したコリジョンクエリの数（目的別・Sweep / Line 別）を記録します。既定は 0（バッファを確保せず、ほぼコストなし）。
  - `ACF.Profile.Capacity <フレーム数>`（既定 1024）: 記録するフレーム数（古いものから上書き）。
  - `ACF.Profile.Report [名前]`: 平均・中央値・P95・最大をログに出し、生データを `Saved/ACFProfile/<名前>_P<番号>.csv` に書きます。
  - `ACF.Profile.Clear`: 記録を消します。
  - `ACF.Profile.Allocations 1` → 操作 → `ACF.Profile.Sites [N]`: ACF の計測区間内のアロケーションの回数・サイズ・発生元を調べます（計測専用。評価が数倍遅くなります）。
- **Unreal Insights**: `-trace=cpu` で起動すると、`ACF_Evaluate` などの区間と Node のクラス名のイベントが見られます（Shipping でも残るのはこのイベントだけ）。
- **基準値の計測**: サンプルマップで `-ACFProfile` を付けて `ActionCameraFramework.Profile.Scenarios` を実行すると、8 つのシナリオの統計を `Saved/ACFProfile/B1_Results.md` に書きます。手順と結果は `Docs/Phase6B_B1_Measurement.md`。
- 計測は出力を変えません（記録 ON / OFF で出力が一致することをテストで確認）。記録やコマンドは Shipping ではコンパイルされません。

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

**Combat Camera / Deoccluder / Constraint Solver（Phase 4B）**

テストマップには敵を 2 体追加しています: `Enemy_4_Pillars`（柱の列の裏）、`Enemy_5_Corridor`（通路の奥）。コマンドで使う敵の番号はラベル順（Enemy_0〜5）です。`ACF.Sample.Enemy Kill` で敵を消すと、その後ろの番号が 1 つずつ詰まります（PIE をやり直すと戻ります）。
サンプルの ThirdPerson / LockOn / Group Preset は、Deoccluder と Framing Recovery を ON にしてあります。比較したいときは `ACF.Debug.Solver 0`（もう一度 `1` で戻す）を使います。

| 確認項目 | 手順 | 期待する見た目 |
|----------|------|----------------|
| 壁越しのロックオン | `ACF.Sample.Goto Start` → `ACF.Sample.Use ThirdPerson` → `ACF.Sample.LockOn 2`（壁の向こうのボス） | 0.25 秒ほど後に、カメラが滑らかに上（または横）へ回り込み、壁越しにボスが見える。`ACF.Debug.Solver 0` にすると壁に隠れたまま |
| 柱の周りを動く敵 | `ACF.Sample.Goto Pillars` → `ACF.Sample.LockOn 4` → `ACF.Sample.Enemy Patrol`（もう一度で停止） | 敵が柱の前を一瞬横切るだけではカメラは動かない（振動しない）。柱の陰に入り続けると回り込み、出てくると 0.5 秒待ってからゆっくり戻る |
| 複数対象の遮蔽 | `ACF.Sample.Goto Start` → `ACF.Sample.Group Use` → `Add 2` → `Add 4` | 見せられる敵が見える位置へ動く。どうしても見えない敵が残る場合も、カメラは上限（Yaw 50° / Pitch 25° / 寄せ 60%）を超えない。`ACF.Debug.Draw 2` の `occluded` に残った数が出る |
| 狭い通路 | `ACF.Sample.Goto Corridor` → `ACF.Sample.LockOn 5` → 歩いて通路に入る | カメラが壁・天井を抜けず、左右に揺れない。回り込めないときは寄せ（Pull In）で対応 |
| Combat Camera の切替 | ThirdPerson で `ACF.Sample.Combat On` → `ACF.Sample.Combat On 0.5` → `ACF.Sample.Combat Off`。ロックオン中にも同じ操作 | On で少し引いて（近い敵ほど大きく）FOV +5°、肩越しが右上へずれる。0.5 では半分。Off でゆっくり元へ戻る。ロックオンの開始・解除と同時でも飛ばない |
| Collision と Group Framing の競合 | `ACF.Sample.Goto Wall`（壁に背を向ける）→ `ACF.Sample.Group Use` → `Add 0` → `Add 4`。比較: `ACF.Debug.Solver 0` / `1` | Solver ON では、壁で寄せられた分を FOV（最大 100°、+25° まで）で補い、敵が枠に戻る。足りないときは `LIMIT` 表示で、プレイヤーの中心は枠内に保たれる。OFF では敵が枠の外へ出る |
| 演出との併用 | 上のどの状態でも `ACF.Sample.Effect Recoil` / `Impulse` / `Shake` | 演出が戦闘カメラ・補正中のカメラの上にかかり、壁を抜けない |

コマンド:
- `ACF.Sample.Combat <On [Intensity=1]|Off>`: ゲームの代わりに戦闘状態を渡します（ロック中ならその敵を距離の基準にします）
- `ACF.Sample.Goto <Start|Wall|Pillars|Corridor>`: Pawn を確認用の位置へ移動します（`NotifyTeleport` も呼びます）
- `ACF.Sample.Enemy Patrol`: ロック中の敵を、柱の列の周りの楕円上で歩かせます（トグル）

**Finisher / Camera Sequence（Phase 4C）**

テストマップに `Enemy_6_Finisher`（開けた場所）を追加しています。サンプルの Sequence `DA_ACFSample_FinisherSequence` は 3 つの Shot でできています。攻撃やアニメーションの代わりに、コンソールコマンドでゲーム側の操作を再現します。

| Shot | カメラ（Preset） | 進み方 | 期待する構図 |
|------|-----------------|--------|--------------|
| 0 `Approach` | `DA_ACFSample_Shot_Approach`（動的。攻撃者 → 対象の線の右 6m、少し後ろ、FOV 60） | 1.2 秒で次へ。0.35 秒のブレンドで入る | 横から見た 2 人。左に攻撃者、右に敵。2 人が動くとカメラも付いて行く |
| 1 `Strike` | `DA_ACFSample_Shot_Strike`（固定。敵の向こう側から攻撃者を見る切り返し、FOV 50） | Advance イベント待ち（タイムアウト 2.5 秒）。カットで入る | 手前に敵の背中、奥に攻撃者。Shot 開始時の位置で止まり、動かない。ヒットの演出（FOV が一瞬狭まり、わずかに揺れる） |
| 2 `Overhead` | `DA_ACFSample_Shot_Overhead`（固定。斜め上の俯瞰、FOV 70） | 1.6 秒で終了。0.6 秒のブレンドで入る | 上から 2 人を見下ろす。終わると 0.6 秒で元のカメラ（ロックオン）へ戻る |
| （狭い場所） | `DA_ACFSample_Shot_Fallback`（攻撃者の肩越し、近い） | 各 Shot の Fallback | 通路の中では横から撮れないので、肩越しのショットに置き換わる。壁を抜けない |

Sequence 全体に、Lifetime = Sequence の「緊張」演出（FOV -3°、Roll 3°）がかかり、終了時に消えます。

| 確認項目 | 手順 | 期待する見た目 |
|----------|------|----------------|
| 再生（ロックオンから） | `ACF.Debug.Draw 2` → `ACF.Sample.Goto Finisher` → `ACF.Sample.Use ThirdPerson` → `ACF.Sample.LockOn 6` → `ACF.Sample.Finisher Play` | ロックオンから Approach へ 0.35 秒で移り、1.2 秒後に Strike へカット。画面左上に `Sequence ... shot 0/3 'Approach'` などが出る |
| イベントで次へ | Strike の間に `ACF.Sample.Finisher Notify Advance`（Anim Notify と同じ経路）または `ACF.Sample.Finisher Advance` | Overhead へブレンドし、1.6 秒後に元のロックオンへ 0.6 秒で戻る。何もしなければ Strike は 2.5 秒で自動的に進む |
| Shot の保持 | Approach の間に `ACF.Sample.Finisher Hold` → 数秒待つ → `ACF.Sample.Finisher Release` | Hold 中は Approach のまま（`HELD` 表示）。Release から残り時間で次へ |
| Shot のスキップ | `ACF.Sample.Finisher Play` → すぐ `ACF.Sample.Finisher Jump Overhead` | Approach・Strike を飛ばして Overhead へ |
| 再生速度 | `ACF.Sample.Finisher Play 6 0.5`、または再生中に `ACF.Sample.Finisher Rate 2` | 0.5 で Approach が 2.4 秒、2 で半分の時間になる |
| キャンセル | 再生中に `ACF.Sample.Finisher Cancel` | 即座に（カットで）元のカメラへ戻る。演出（Roll など）も即座に消える |
| 正常終了 | 再生中に `ACF.Sample.Finisher Finish` | 0.6 秒で元のカメラへ戻る |
| 対象の破棄 | 再生中に `ACF.Sample.Enemy Kill`（ロック中の敵 = Finisher の対象） | Finisher が終わり、元のカメラへ戻る（ロックオンも解除されるので通常カメラへ）。クラッシュしない |
| 割り込み | 再生中に `ACF.Debug.PushFixed 1300 -800 700 -45 -90 0 70 5000`（Priority 5000 の固定カメラ）→ 少し待って `ACF.Debug.ReleaseLast` | 割り込み中は固定カメラ（`not in control` 表示）。時間は進み続け、解除すると続きの Shot が映る |
| 狭い場所（Fallback） | `ACF.Sample.LockOff` → `ACF.Sample.Goto CorridorFinisher` → `ACF.Sample.Finisher Play 5` | 通路の壁で横からの Approach が成立しないので、`FALLBACK` 表示で肩越しのショットになる。どの Shot でもカメラが壁・天井を抜けない |
| Collision の確認 | 上の状態で `ACF.Debug.Draw 1` | Collision の Sweep が「演出の中心 → カメラ」で描かれ、壁の手前で止まる |
| 演出の確認 | 再生中・終了後に `ACF.Debug.Draw 2` の Effect 行を見る | 再生中は Tension（と Strike の Hit）が表示され、終了後（Release の時間の後）には何も残らない |
| 単独 Preset の Finisher | `ACF.Sample.Finisher Preset 2`（2 秒）/ `ACF.Sample.Finisher Preset`（Finish まで） | Approach の Preset だけで 0.3 秒で入り、時間または Finish で 0.5 秒で戻る |

コマンド:
- `ACF.Sample.Finisher Play [敵の番号] [PlayRate=1]`: 番号を省略するとロック中の敵、いなければ一番近い敵が対象です
- `ACF.Sample.Finisher Preset [Seconds=0]`
- `ACF.Sample.Finisher <Advance|Hold|Release|Finish|Cancel|Status>`、`Jump <Approach|Strike|Overhead>`、`Rate <PlayRate>`
- `ACF.Sample.Finisher Notify <Advance|Hold|Release|Finish|Cancel>`: Pawn を Actor として `Send ACF Camera Sequence Command For Actor` を呼びます（Anim Notify と同じ経路）
- `ACF.Sample.Goto <Finisher|CorridorFinisher>`: Enemy_6 の 3.5m 手前 / 通路の中で Enemy_5 の 3m 手前

**Level Sequencer（Phase 5A）**

サンプルの Level Sequence が 2 本あります（`Scripts/CreateManualTestAssets.py` で再生成。カメラは Sequence 内の Spawnable なので、どのマップでも再生できますが、位置は `L_ACFCameraTest` のスタート地点に合わせてあります）。

| アセット | 内容 | 終わり方 |
|----------|------|----------|
| `LS_ACFSample_Cut`（3 秒） | `Cam_Wide`（スタート地点を高い位置から見下ろす、1.5 秒）→ カット → `Cam_Low`（低い位置からあおる、1.5 秒） | カットで終わる → ACF の Return Blend（既定 EaseInOut 0.5 秒） |
| `LS_ACFSample_Blend`（4 秒） | `Cam_Dolly`（左後方から右へ移動するドリー）。Camera Cut Section に Ease In / Out 各 1 秒 | Sequencer の Ease Out（PCM の ViewTarget Blend）で戻る。ACF は何も重ねない |

準備: `ACF.Sample.Goto Start` → `ACF.Sample.Use ThirdPerson` → `ACF.Debug.Draw 2`（必要なら `showdebug camera`）。

| 確認項目 | 手順 | 期待する見た目 |
|----------|------|----------------|
| カットで入って戻る | `ACF.Sample.Sequencer Play Cut` | すぐに高い位置のカメラに切り替わり（ACF のカメラは混ざらない）、1.5 秒後に低い位置のカメラへカット。終わると 0.5 秒でゲームプレイカメラへ滑らかに戻る。戻りの最初のフレームで跳ばない |
| Ease In / Out | `ACF.Sample.Sequencer Play Blend` | 1 秒かけてゲームプレイカメラからドリーへ滑らかに移り、最後の 1 秒で戻る。速度は一定に上がって下がり、途中で段がついたり二重にブレンドしたりしない |
| 途中停止・一時停止 | `Play Blend` の途中で `ACF.Sample.Sequencer Pause` → `Resume` → `Stop` | Pause 中はそのカメラで止まる。Stop でその場の姿勢から 0.5 秒で ACF カメラへ戻る |
| 連続再生 | 再生中にもう一度 `ACF.Sample.Sequencer Play Cut` | 破綻せず、最後は ACF カメラへ戻る |
| ロックオン中 | `ACF.Sample.LockOn 0` → `ACF.Sample.Sequencer Play Cut` → 終わるまで待つ | ロックオンのカメラ（Enemy_0 を捉えた構図）へ戻る。再生中に `ACF.Sample.Enemy Kill` すると、戻り先は通常カメラになる |
| Finisher 中 | `ACF.Sample.Goto Finisher` → `ACF.Sample.LockOn 6` → `ACF.Sample.Finisher Play` → すぐ `ACF.Sample.Sequencer Play Cut` | 既定（Continue）では裏で Finisher の時間が進み、戻ると続きの Shot（終わっていればロックオン）が映る |
| 戻り方の調整 | `ACF.Sample.Sequencer ReturnBlend 0` / `2`、`ControlRotation Match` / `Keep`、`Reset` | 0 でカット、2 で 2 秒かけて戻る。Match では Sequence の最後の向きのまま操作に戻る |
| 状態 | `ACF.Sample.Sequencer Status` | `phase=InControl bypassed=1 suppressed=0` など（再生中）。終わると `phase=None bypassed=0` |
| 外部 ViewTarget（Sequencer なし） | （コマンドはありません）BP で `Add ACF External View Target` → `Set View Target with Blend` | 自動テスト `.Sequencer.ExternalViewTargetCoreApiAndViewTargetBlend` で確認済み。登録した Camera Actor はそのまま映り、PCM のブレンドだけで行き来する |

コマンド:
- `ACF.Sample.Sequencer Play <Cut|Blend|アセットパス> [PlayRate=1]`: Level Sequence Actor を作って再生します（前の再生は止めます）
- `ACF.Sample.Sequencer <Stop|Pause|Resume|Status|Reset>`、`ReturnBlend <秒>`、`ControlRotation <Keep|Match>`

## 10. 拡張（プログラマー向け）

`UACFCameraNode` を継承し、`GetStage()`（Body / Aim / Framing / PostBlend）と `Evaluate` を実装します。

- Node のメンバーは共有設定です。実行時に書き換えないでください。
- フレームごとに変わる値は USTRUCT に入れ、`GetRuntimeStateType()` で返します。
- テレポートには `ResetInterpolation`、有効化には `OnActivated` で対応します。
- `DeltaTime <= 0` のときは状態を進めないでください。
- Body の Node は `Context.Scratch.Pivot` を設定してください（Collision の Sweep 起点になります）。
- Framing 段の Node は位置と FOV だけを変え、回転は変えないでください。
- 同じ段の中で先に動きたい Node は `GetStageOrder()` を小さくします（例: Combat Camera は -100 で Group Framing より先）。
- 制約の解決は `FACFCameraConstraintSolver`（`Deocclude` / `RecoverFraming`。ワールドへの問い合わせは関数で渡すので、ワールドなしでテストできます）。
- 共通部品: `FACFCameraTargetTrackingState::Track` / `TrackPoint`（対象喪失・テレポート・Lookahead）、`UpdateDamped`（軸別 Damping）、`ACFFraming`（画面への投影・構図）、`ACFTargetGroup::GatherSubjects`（Target Group の解決）、`UACFCameraNode_GroupFraming::ComputeRequiredDolly / ComputeRequiredFov`、`ACFCameraCollision::SweepCamera`
- シーケンス: `FACFCameraSequencePlayer`（Director が所有し、Evaluate の中で 1 フレーム 1 回 Tick）。Shot は普通の要求なので、Shot 用の Node も通常の Node と同じ作り方です。Shot の成立判定には `UACFCameraDirector::ProbeCamera`（状態を持たない一時的な Rig で 1 回評価）を使えます。要求の削除で戻りのブレンドを指定するには `RemoveRequest(Handle, &Blend, /*bOverrideAnyReturn*/ true)`。
- 新しい演出: `UACFCameraEffect` を継承し、`Evaluate` で `FACFCameraEffectOutput`（チャンネル・値・空間）を返します。状態は `GetRuntimeStateType`、後始末は `OnEnd` で行います。Envelope・Weight・減衰・合成はスタック側が行います。

## 11. 制限（Phase 5A 時点）

**Finisher / Camera Sequence**
- シーケンス時間は Montage の再生位置を読みません（ACF はアニメーションに依存しない）。Duration だけで進む Shot は、Montage の速度変更に Play Rate で合わせる必要があります。正確に合わせたい切替は Anim Notify（イベント）で行ってください。
- 攻撃者以外の Actor の Custom Time Dilation（例: 対象だけのヒットストップ）は反映しません。必要なら Hold / Play Rate で制御します。
- Shot の成立判定（Probe）は Shot の開始時の 1 回だけです。Shot の途中で狭くなっても別の Shot には切り替えず、Collision と（ON なら）Deoccluder で対応します。Probe は「本来の距離の何割まで寄せられたか」と「攻撃者・対象の中心への視線」だけを見ます。
- Fallback は Shot ごとに 1 つです。Fallback 自体が成立するかは確認しません（Collision で寄せたまま再生します）。
- Finisher の終了・Cancel 以外で、戻り先カメラのブレンドを再生側から指定する API はありません（ロックオン解除と同じく、通常の要求の解除は戻り先の Blend In）。
- 通知（On Camera Sequence Event）はカメラ更新の直後にまとめて送られます（Finish / Cancel は呼んだ直後）。カメラ更新の途中には送りません。
- Finisher の再生中に Level Sequence の Camera Cut が割り込むと、Interrupt Policy に従います（14 章）。
- 実機での演出の見栄え（各 Shot の構図・ブレンド時間の既定値）は、サンプルのスクリーンショットでしか確認していません。

**Combat / Deoccluder / Constraint Solver**
- 遮蔽物を透過させるマテリアル処理はありません。カメラを動かしても見えない対象は通知（`NumOccludedTargets`）だけです。
- Deoccluder の候補は固定の 10 通り（回り込み・持ち上げ・寄せ）です。複雑な地形で、候補の間にしか見える位置がない場合は見つけられません。
- 確認するのは各対象の中心 1 点への視線です。体の一部だけが見えている状態は「見えない」と判定されます。
- 壁際で Collision が大きく寄せると、FOV の上限で足りず、敵が枠から出て、プレイヤーが画面の大部分を占めることがあります（テストマップの「競合」項目で再現。定義どおりの縮退動作）。
- Combat Camera のアーム長は Body Node のピボットを中心に伸ばします。ピボットの Damping による遅れも同じ倍率で伸びます。
- 実機での操作感（補正の速さ・ヒステリシス時間の既定値）は未調整です。

**Lock-On / Target Group**
- Collision と Group Framing の競合は Framing Recovery（FOV、上限つき）で補います（Phase 4A の制限を解消）。
- ロックオン中の移動入力の基準（ControlRotation）は変更しません。解除時にカメラの向きへ合わせるかどうかはゲーム側で決めます（サンプルは合わせます）。
- ロックオンの解除は戻り先カメラの Blend In を使います。解除専用のブレンドを指定する API はありません。
- Group Framing はメンバーの位置を平滑化しません（Lock-On のロック点だけは平滑化されたものを使います）。急な追加は Widen Damping Time で滑らかになりますが、メンバー自身の瞬間移動は枠の調整が追いかける形になります。
- Weight の小さいメンバーは、`Weight / 最大 Weight` の割合で枠に入れる扱いになるので、完全には収まりません（仕様）。

**全般**

- 描画結果の目視確認と、実機のマウス・パッドでの操作感確認は、まだ行っていません（9 章の手順で確認してください）。
- PCM の ViewTarget ブレンド中の壁抜けは防げません（7 章）。
- ブレンド中に割り込むと、割り込まれた側の動きはそれ以降反映されません。
- Collision は「ピボット → カメラ」の線上だけを見ます。視線上の遮蔽物は Deoccluder（Collision Node の設定）で扱います。
- SpringArm と一致することは、静止時（Lag 収束後）と障害物ありで確認しました。移動中の過渡応答（Lag の式）は異なります。
- Blueprint 製の Node / Effect は未対応です。
- 演出は ACF カメラにかかります。UE カメラだけの場面では、素通し Preset を要求してください（6 章）。
- Shake の PostProcess 出力は PCM に渡します。ただし PCM が作り直されると、それ以降の PostProcess は失われます（位置・回転・FOV は影響を受けない）。
- 演出のステージは 2 つ（Collision の前と後）で固定です。演出ごとに段を選ぶことはできません（位置は前、回転・FOV は後）。

## 12. 検証

- 検証用アセットは、どちらも作り直せます（既存のアセットは再利用し、グラフだけ書き直します）。プラグインのフォルダーで `Scripts/RunEditorPython.sh Scripts/<スクリプト>.py`（22 章）。
  - `Content/Verification/`（DA ×3、BP ×2）: `Scripts/CreateVerificationAssets.py`
  - `Content/Samples/`（カメラ Preset ×10、Effect Preset ×2、Camera Sequence ×1、Level Sequence ×2、テストマップ）: `Scripts/CreateManualTestAssets.py`
- 自動テストは 168 件です（実行方法とテスト環境は 22 章）。
  - Phase 6B / B-9: `.Effects.SpeedFovAndRollFixedStep`（Editor とゲームの両方。固定 DeltaTime の Speed FOV / Roll）、`.TestEnvironment.*`（2 件、`-game`。状態の復元と LocalPlayer の後始末）、`.Integration.SpeedFovAndRollRepeated.Run1〜3`（同じシナリオの連続実行）。
  - `.PresetValidation.*`（12 件。Editor とゲームの両方）と `.Editor.*`（2 件。Editor のみ）: Preset の検証（21 章）。
  - `.Director.*` / `.Nodes.*` / `.ThirdPerson.*` / `.Effects.*` / `.Action.*` / `.Constraints.*` / `.Sequence.*` / `.Authority.*`（58 件）: Editor とゲームの両方で実行できます。
  - `.Profiler.*`（5 件。計測の ON / OFF で出力が変わらないこと、Trace 数・アロケーションの数え方、一時停止・複数 LocalPlayer）: 3 件は Editor とゲームの両方、2 件は `-game` のみ。`.Profile.Scenarios`（1 件）は性能の基準値の計測で、`-ACFProfile` を付けてテストマップで実行したときだけ動きます（8 章）。
  - `.Health.*`（10 件。Editor とゲームの両方）と `.Sequencer.Health.*`（2 件、`-game`）: 不正値（NaN / Inf / 不正な FOV / 不正な DeltaTime など）の対策（16 章）。正常な入力で姿勢が修正前とビット単位で一致すること、ランダム化した不正入力を含む。
  - `.Lifetime.*`（5 件。Editor とゲームの両方）: Actor の参照と寿命（17 章）。実際の World・Actor・GC を使い、`-DisableGarbageElimination` でも合格すること。 B-5 の演出の寿命（要求の有無と World の変化）を含む。
  - `.Integration.*`（24 件 + B-9 の 3 件）、`.Sequencer.*`（21 件。実際の Level Sequence・PCM を使う。うち `.Sequencer.PcmCompat.*` 7 件は Phase 6B / B-2 の PCM 互換性、15 章）: `-game` でのみ実行されます。
  - `.Samples.*`（6 件: TestMapSmoke、TestMapScreenshot、LockOnAndGroupOnTestMap、SolverAndCombatOnTestMap、FinisherOnTestMap、SequencerOnTestMap）: テストマップ上でだけ実行されます。他のマップではスキップします。スクリーンショットは描画あり（`-NullRHI` なし）のときだけ `Saved/Screenshots/` に保存されます。
- Phase 4A で追加したテスト（`.Action.*` 10 件、`.Integration.LockOnCollisionEffectsAndDestroy`、`.Samples.LockOnAndGroupOnTestMap`）の確認内容:
  - ロック開始・解除（ブレンド中に 1 フレーム 40cm 超の飛びがない）、敵が周回・プレイヤーが移動しても両方が画面内
  - 近距離・遠距離のアーム長と FOV、重なり・真上でも NaN にならず Yaw を保持
  - 敵の瞬間移動・ターゲット切替は滑らか、プレイヤーのテレポートはカット、破棄で保持または自動解除（切替後の旧対象の破棄では解除しない）
  - ブレンド中のターゲット切替、高優先度カメラへの切替と復帰、出力抑制中も評価が続き復帰時に最新、同一フレームの二重評価で状態が進まない
  - Group の追加・削除・破棄メンバーの自動除外、Weight / Radius による変化、0 件・1 件、FOV モード、Fill、Distance Then FOV、必要距離・必要 FOV の解析解との一致
  - 実際の PCM 上で Collision（Guard OFF）と Effects の併用、テストマップ上でのコンソールコマンド一式
- Phase 4B で追加したテスト（`.Constraints.*` 7 件、`.Integration.Constraints*` 3 件、`.Samples.SolverAndCombatOnTestMap`）の確認内容:
  - 遮蔽なし（補正なし、1 フレーム Sweep 1 + 対象数のトレース）、一時的な遮蔽・点滅する遮蔽（動かない）、継続的な遮蔽（遅延後に 1 回の探索で解決、補正は滑らか、障害物が消えると待ってから戻る）
  - 複数対象（見せられる敵は見せ、見せられない敵は通知、探索は間引かれる）、プレイヤーが見えない場合（上限内・通知・NaN なし）、対象の破棄
  - Collision と Group Framing の競合（Solver OFF で再現 → ON で FOV により解消）、FOV 上限（上限で止まり LIMIT を通知、プレイヤーは枠内）、プレイヤーが枠外のときの回転（上限つき）
  - 実際のジオメトリ: 柱の裏のロックオン（実際の Line Trace で見えることを確認）、壁際のロックオン、狭い通路（壁・天井を抜けず、0.75 秒未満の左右の切り返しなし）
  - Combat の状態切替（距離・FOV・肩越し・Intensity・Profile 差し替え・滑らかな出入り）、ブレンド中の Combat、Effect との併用、出力抑制中、1 フレームの二重評価（Combat と Solver の両方）
  - テストマップで 4B のコンソールコマンド一式（壁越しのボス、柱を巡回する敵、通路、戦闘切替、壁際のグループ）と、遮蔽の通知が実際の視線と一致すること
- Phase 4C で追加したテスト（`.Sequence.*` 6 件、`.Integration.Finisher*` 3 件、`.Samples.FinisherOnTestMap`）の確認内容:
  - Finisher の開始・終了（同じフレームでカット、Duration で終了、Return Blend で戻る、Duration 0 は Finish まで待つ、古い Handle は無効）
  - 複数 Shot の順次再生（Cut と Blend の姿勢を毎フレーム確認、要求は常に 1 つだけ、通知の順番）、ヒッチで Shot を跨いだときの繰り越し
  - イベントでの進行（同じフレームの Advance は 1 回）、スキップ（Jump、名前の誤りは拒否）、Hold（回数）、Play Rate、Wait For Event のタイムアウト、同一フレームの二重評価で時間もイベントも進まない
  - Cancel（要求と演出が即座に消え、カットで戻る）、演出の所有（Shot / Sequence / Fire And Forget の寿命どおりに止まり、何も残らない）、別の再生による置き換え、要求を外から消されたとき、Instigator の破棄
  - 高 Priority の割り込み（Continue / Pause / Cancel）、ロックオン → Finisher → 戻り（Return Blend が戻り先の Blend In より優先、後の切替には漏れない）、ローカルプレイヤー 2 人の独立
  - 実際の PCM: Shot Anchor の動的ショット（追従）と固定ショット（動かない）、対象の破棄で終了、狭い場所の Fallback / Skip / 寄せたまま再生、Shot ごとの Deoccluder（柱の裏の対象が実際の視線で見える / OFF では見えないまま）、Finisher 中の Collision Guard
  - Anim Notify / Notify State での進行・保持・終了（Actor 経由のルーティング）、攻撃者の Custom Time Dilation、Pause 中は時間が止まる
  - テストマップで 4C のコンソールコマンド一式（3 Shot の構図で攻撃者と敵が画面内、ロックオンへ戻る、演出が残らない、通路では Fallback、Cancel でカット）
- Phase 5A のテスト（`.Sequencer.*` 14 件、`.Authority.*` 3 件、`.Samples.SequencerOnTestMap`）の確認内容:
  - カットで入り / 戻る Sequence（裏で毎フレーム評価、Weight は 1 のまま、Return Blend の最初のフレームで跳ばない）、Ease In / Out（PCM のブレンド 1 本だけ。プローブ Modifier で ViewTarget ごとの出力を確認）
  - Pause / Stop / 連続再生 / Level Sequence Actor の破棄、Keep State で終わる Sequence（ゲームがブレンド / カットで戻す）、終了と次の Camera Cut が同じフレーム（Stop + Play、OnFinished から再生）、重なる Sequence
  - Camera Cut Track の無い Sequence、Camera Cut がゲームプレイの ViewTarget を指す Sequence、Camera Component を持つ普通の Actor、出力抑制の重複と解除順序
  - Core の外部 ViewTarget API（Sequencer なし。未登録の Camera Actor、登録・Source の破棄、`SetViewTargetWithBlend` での出入り、`Blend ACF Camera From Pose`）
  - Lock-On・Finisher（Continue / Pause / Cancel）との併用、ControlRotation、PCM の作り直し、ローカルプレイヤー 2 人の独立
  - テストマップで `LS_ACFSample_Cut` / `LS_ACFSample_Blend` とロックオン中の再生（描画あり、スクリーンショット `ACF_5A_*`）

実行はプラグインのフォルダーで `Scripts/RunTests.sh`（22 章）:

```bash
Scripts/RunTests.sh
```

```bash
Scripts/RunTests.sh --context editor
```

```bash
Scripts/RunTests.sh --context samples
```

## 13. ファイル構成

| パス | 内容 |
|------|------|
| `Source/ActionCameraFramework/Public/` | `ACFCameraSubsystem`（入口）、`ACFCameraDirector`、`ACFCameraSequence`（Sequence / Shot の定義・Handle・状態・通知）、`ACFCameraSequencePlayer`（再生）、`ACFCameraAnimNotifies`（Anim Notify / Notify State）、`ACFCameraConstraintSolver`（Deoccluder / Framing Recovery）、`ACFCameraPreset`、`ACFCameraNode`（段・Scratch）、`ACFCameraRigInstance`、`ACFCameraOutputModifier`（Output と Guard）、`ACFCameraTypes`、`ACFCameraSettings`、`ACFCameraBlueprintLibrary`、`ACFCameraProfiler`（計測。Shipping 以外） |
| `Source/ActionCameraFramework/Public/Nodes/` | FixedPose / Follow / LookAt / OrbitalFollow / Composers / LockOn / ShotAnchor / Combat / GroupFraming / Collision と、共通部品の TargetTracking（Damping・Lookahead・テレポート）/ Framing（画面座標の計算）/ TargetGroup（グループの解決） |
| `Source/ActionCameraFramework/Public/Effects/` | `ACFCameraEffect`（Layer 基底・Effect Preset）、`ACFCameraBuiltinEffects`（Punch / Recoil / Impulse / Shake / Speed FOV / Roll）、`ACFCameraEffectStack`（Instance・Envelope・合成）、`ACFCameraEffectTypes` |
| `Source/ActionCameraFramework/Private/` | 上記の実装、`ACFCameraDebugDraw`、`ACFCameraDebugCommands`、`ACFCameraDebugShake`（サンプル用 Shake）、`Tests/` |
| `Source/ActionCameraFrameworkSequencer/` | Level Sequencer 連携（Phase 5A）。`ACFSequencerCameraSubsystem`（検出・復帰処理）、`ACFSequencerBlueprintLibrary`、`ACFSequencerSettings`、`ACFSequencerTypes`、`ACF.Sample.Sequencer` コマンド、`Tests/`。LevelSequence / MovieScene に依存するのはこのモジュールだけです |
| `Source/ActionCameraFrameworkEditor/` | Editor モジュール（Phase 7A の Editor 専用機能の置き場。現状は Data Validation 経由の検証テストだけ）。Preset の検証の本体は `ACFCameraPresetValidation`（ランタイムモジュール、`WITH_EDITOR` のみ） |
| `Content/Verification/` | 自動テスト用アセット |
| `Content/Samples/` | 手動確認用のサンプル Preset（Shot 用を含む）、Effect Preset、Camera Sequence、Level Sequence（`LS_ACFSample_Cut` / `LS_ACFSample_Blend`）とテストマップ `L_ACFCameraTest` |
| `Scripts/` | 上記アセットの生成スクリプト |
| `Docs/`（Git 管理外） | 引き継ぎ用の詳細資料（`Roadmap.md` = 開発ロードマップ、`Audit_Phase6A.md` = 監査結果と Phase 6B の計画、`Phase6B_B1_Measurement.md` = 計測の仕組みと性能の基準値、設計判断、Effect Pipeline 設計）。計測の生データは `Saved/ACFProfile/` に出力され、Git 管理しません |

## 14. Level Sequencer との連携（Phase 5A）

Level Sequence の Camera Cut がカメラを握っている間、ACF は出力を止めて裏で評価を続け、終わるとゲームプレイカメラへ戻ります。
設定なしで動きます（`ActionCameraFrameworkSequencer` モジュール。基本の Runtime モジュールは Sequencer に依存しません）。

> **状態**: 完了。自動テスト（`.Sequencer.*` 14 件、`.Authority.*` 3 件、`.Samples.SequencerOnTestMap`）と Phase 1〜4C の回帰テストは全件合格しています（12 章）。
> 描画ありのテストマップでカメラの切替と復帰を確認しました。Editor（PIE）で人が操作して見る確認は未実施です（9 章の手順）。

**実際の呼び出し経路（UE 5.8 のソースとテストで確認）**
- `APlayerCameraManager::DoUpdateCamera` は ViewTarget と、ブレンド中なら PendingViewTarget について `UpdateViewTarget` を 1 回ずつ呼び、どちらも最後に `ApplyCameraModifiers(DeltaTime, VT.POV)` で **その ViewTarget の POV を参照のまま** 全 Modifier に渡します。ViewTarget が Camera Actor でも Modifier は呼ばれます（デバッグ用の CameraStyle を除く）。
- そのため ACF の Output Modifier は「いま更新している POV が ViewTarget / PendingViewTarget のどちらか」をアドレスで判別し、外部 ViewTarget の POV には書き込みません。テストでは ACF の直後（Priority 65）に置いたプローブ Modifier で、Ease In / Out・`SetViewTargetWithBlend` の間、1 フレームに 2 回呼ばれ、ゲームプレイ側の POV には ACF の姿勢が 100%、外部カメラ側の POV は手つかずで次の Modifier に届くことを確認しています。
- Camera Cut は `MovieSceneCameraCutGameHandler` が `PC->SetViewTarget(カメラ, TransitionParams)` を呼びます（Ease In / Out は PCM の ViewTarget Blend）。カットで終わるときは保存した ViewTarget へ `SetViewTarget` で戻し、`bClientSimulatingViewTarget` を下ろします。Ease Out で戻るときは、ブレンド中も `bClientSimulatingViewTarget` が立ったままです。
- 外部 ViewTarget しか更新されなかったフレームは、カメラ更新の後（`OnWorldPostActorTick`。UE 5.8 ではカメラ更新より後に呼ばれ、Pause 中は呼ばれない）に 1 回だけ裏評価します。

**仕組み（UE 5.8 の実装に基づく）**
- Camera Cut は `PlayerController::SetViewTarget` で PCM の ViewTarget を Sequencer のカメラに差し替えます。対象のプレイヤーは Level Sequence Actor の Owner（PlayerController のとき）、無ければ最初のローカルプレイヤーです。
- UE 5.8 の PCM は、ViewTarget が Camera Actor でも Camera Modifier を適用します。何もしなければ ACF が Sequencer のカメラを上書きします。
- そこで Sequencer のカメラを ACF の「**外部 ViewTarget**」として登録し、ACF はその ViewTarget にだけ出力しません。
- 判定は ViewTarget 単位です。Camera Cut の Ease In / Ease Out（PCM の ViewTarget Blend）の間も、ゲームプレイ側の ViewTarget には ACF が 100% で乗り続け、2 つを混ぜるのは PCM のブレンドだけです。ACF の Weight は動かさないので、**ブレンドが二重になりません**。
- Sequencer を ACF の Camera Sequence へ変換することはしません。ACF の要求・Rig の状態にも触れません。

**カメラの所有権**

| 状態（Phase） | 画面 | ACF |
|---------------|------|-----|
| None | ゲームプレイカメラ | 通常どおり出力 |
| Blending In（Ease In） | PCM がゲームプレイ → Sequencer へブレンド | ゲームプレイ側の ViewTarget にだけ 100% で出力 |
| In Control | Sequencer のカメラ | 出力なし。**裏で評価を続ける**（`Is ACF Output Bypassed` = true） |
| Blending Out（Ease Out） | PCM が Sequencer → ゲームプレイへブレンド | ゲームプレイ側の ViewTarget にだけ 100% で出力 |

**Sequencer 中の ACF の内部状態（継続方式を採用）**
- 「評価を止めて復帰時に作り直す」方式と比べ、継続方式は復帰した最初のフレームから最新の構図を出せます（Damping が収束済みで、戻ってからカメラが追いかけて滑らない）。PCM がブレンドで戻る場合は、ブレンドの最初のフレームから正しい戻り先が必要なので、継続が必須です。
- 裏で進むもの: Damping、要求間のブレンド、Lock-On の追従と対象破棄による自動解除、Combat の切替、演出の寿命、Instigator の破棄による解除。
- コスト: ゲームプレイ中と同じ評価（Collision のトレースを含む）が Sequence 中も 1 フレーム 1 回走ります。
- 裏評価の UE カメラ（Upstream）は、最後に ACF を出力した ViewTarget（通常は Pawn）のカメラです。`Get Aim Camera Pose` もその間はゲームプレイカメラを返します。画面の姿勢は `Get Visual Camera Pose` を使ってください。

**Lock-On / Combat / Finisher との関係**
- Lock-On・Combat の要求と状態は Sequence の間も残り、終わるとそのカメラへ戻ります。
- Finisher（Camera Sequence）は、Sequencer に画面を奪われると **Interrupt Policy** に従います: Continue（時間は進む）/ Pause（止まる）/ Cancel（終了）。`Suppress Output` による抑制も同じ扱いです。

**終了時の戻り方**

| Sequencer の戻り方 | ACF の動作 | Return Kind |
|--------------------|-----------|-------------|
| Camera Cut の Ease Out（ブレンド） | 何も重ねない（PCM のブレンドだけ） | Sequencer Blend |
| カット（再生終了の Restore、Stop、途中停止、Level Sequence Actor の破棄） | **Return Blend**（既定 EaseInOut 0.5 秒）で「前フレームに映っていた Sequencer の姿勢 → ACF カメラ」を補間 | Acf Return Blend |
| カット、かつ Return Blend が Cut / ACF の要求が無い | そのままカット | Cut |

- どの終わり方でも「ViewTarget がゲームプレイ側へ戻った」ことで復帰を処理するので、終わり方ごとの呼び出しは不要です。一時停止（Pause、Pause At End）中は Sequencer が握ったままです。
- **ControlRotation**: 既定（Keep）は変更しません。ゲームプレイカメラは開始前の向きのまま戻ります。`Match Sequencer Camera` にすると、ゲームプレイカメラが見え始める時点で Sequencer の最後の向き（Roll 0、Pitch は PCM の制限内）を ControlRotation に入れ、Orbital Follow（Input Source = Camera Input）の向きも合わせます。

**設定**（優先順: Level Sequence Actor 単位 > プレイヤー単位 > Project Settings > Plugins > Action Camera Framework - Sequencer）

| 項目 | 意味 |
|------|------|
| Automatic Handling（Project Settings のみ） | OFF にすると検出も通知も行いません |
| Enabled | OFF にすると、その対象では ACF が Sequencer のカメラの上にそのまま出力され、復帰処理も行いません |
| Return Blend | カットで戻ったときに ACF が補うブレンド。Cut にすると Sequencer のカットを尊重します |
| Control Rotation Policy | Keep / Match Sequencer Camera |
| Suppress Output While Playing / Output Blend Time | Camera Cut が無くても、その Sequence の再生中は ACF の出力を抑制します（プレイヤー自身の Camera Component を Sequencer で直接動かす演出向け。Level Sequence Actor 単位の設定でだけ有効） |

**Blueprint / C++ API**

| ノード | 内容 |
|--------|------|
| `Get ACF Sequencer Camera State` (Player Controller) | Phase、再生中の Sequence 数、握っている Level Sequence Actor、Sequencer の ViewTarget、戻り先の ViewTarget、抑制中か、バイパス中か |
| `Is Sequencer Controlling ACF Camera` (Player Controller) | Camera Cut が画面を握っているか（ブレンド中を含む） |
| `Set / Clear ACF Sequencer Options For Player` | プレイヤー単位の設定の上書き |
| `Set / Clear ACF Sequencer Options For Sequence` | Level Sequence Actor 単位の設定の上書き（Play の前に呼ぶ） |
| `Get ACF Sequencer Camera Subsystem` → `On Sequencer Camera Started` / `On Sequencer Camera Ended` | 開始 / 終了（Return Kind つき）のイベント。カメラ更新の外で配信（19 章） |
| `Add / Remove ACF External View Target` (View Target, Source)、`Is ACF External View Target` | ゲーム独自のカットシーンカメラを「ACF を乗せないカメラ」として登録（Subsystem のノード） |
| `Blend ACF Camera From Pose` (Pose, Blend) | 任意の姿勢から、いま有効な ACF カメラへブレンドして戻る。Blueprint で理由つき・プレイヤー指定で使うなら `Blend ACF Camera From Pose / Current View For Player`（20 章） |
| `Is ACF Output Bypassed` / `Get Num ACF Output Suppressions` / `Set All ACF Orbit Rotations` | 状態の確認、Orbit の向きの一括設定 |

```cpp
// 状態とイベント
UACFSequencerCameraSubsystem* Sequencer = UACFSequencerCameraSubsystem::Get(this);
Sequencer->OnSequencerCameraEnded.AddDynamic(this, &AMyGameMode::OnCinematicEnded);
// この Sequence だけ、カットで戻り、最後の向きから操作を再開する
FACFSequencerCameraOptions Options;
Options.ReturnBlend = FACFCameraBlendSettings::MakeCut();
Options.ControlRotationPolicy = EACFSequencerControlRotationPolicy::MatchSequencerCamera;
Sequencer->SetSequenceOptions(SequenceActor, Options);
// ゲーム独自のカットシーンカメラ（Sequencer を使わない場合）
Cameras->AddExternalViewTarget(CutsceneCameraActor, this);
PlayerController->SetViewTargetWithBlend(CutsceneCameraActor, 1.f);   // PCM のブレンドだけで移る
```

**出力抑制が重なる場合**: 抑制は Source ごとに数えます（同じ Source の重複は 1 つ）。どの順番で解除しても、最後の 1 つが外れるまで出力は戻りません。破棄された Source は自動で外れます。Camera Cut では抑制を使わない（外部 ViewTarget）ので、ゲーム側の `Suppress Output` と干渉しません。

**ローカルプレイヤー**: 状態・設定・外部 ViewTarget の登録はローカルプレイヤーごとに独立です。PlayerController / PCM が作り直されると、そのプレイヤーの判定だけをやり直します。

**手動確認**: 9 章の「Level Sequencer（Phase 5A）」（サンプル `LS_ACFSample_Cut` / `LS_ACFSample_Blend`）。任意の Level Sequence も `ACF.Sample.Sequencer Play <アセットパス>` で再生できます。レベルに置いた Level Sequence Actor を普通に再生しても同じように検出されます。

**自動テストの実行**

```bash
Scripts/RunTests.sh "ActionCameraFramework.Sequencer+ActionCameraFramework.Authority"
```

**制限**
- 検出は PCM の ViewTarget と、Camera Cut が立てる `bClientSimulatingViewTarget`、追跡している Level Sequence Actor の再生状態から行います。Level Sequence Actor を使わない再生（独自の MovieScene プレイヤー）は検出しません。その場合は `Add ACF External View Target` を使ってください。
- Camera Cut が ViewTarget を戻さない設定（Keep State）で終わった場合、同じカメラが映っている限り「握っている」とみなします。ゲームが ViewTarget を戻した時点で復帰を処理します。
- Camera Cut がプレイヤーの Pawn 自身（ゲームプレイの ViewTarget）を指す場合は、Sequencer のカメラとはみなしません（ACF が乗ったままです）。必要なら Suppress Output While Playing を使ってください。
- 外部 ViewTarget の判定は、PCM が ViewTarget の POV を参照のまま Modifier へ渡すときに正確です。POV をコピーして渡す派生 PCM での判定と制限は 15 章を参照してください。
- PCM が作り直された直後は、Sequencer が Camera Cut を掛け直すまでの 1 フレームほどゲームプレイカメラが見えます。
- Return Blend は ACF の要求があるときだけ働きます（UE カメラだけの場面ではカットのままです）。
- ゲームが登録せずに `SetViewTarget` / `SetViewTargetWithBlend` で Camera Actor へ切り替えた場合、ACF はその Camera Actor の上にも乗ります（ACF は ViewTarget に関係なく上に乗るレイヤー。Phase 4C までと同じ）。ACF を乗せたくないカメラは `Add ACF External View Target` で登録してください。
- ゲーム独自の外部 ViewTarget からカットで戻した場合、ACF は自動では補間しません（Sequencer 連携だけが行う）。滑らかにしたい場合は `Blend ACF Camera From Pose` を呼ぶか、`SetViewTargetWithBlend` で戻してください。 手順は 20 章。
- Sequence が重なって再生された場合（A の再生中に B）、どのカメラを映すかは UE の Camera Cut の規則（後から始まった Sequence が優先、全部終わるとまとめて戻る）に従います。ACF は「最初の開始 → 最後の終了」を 1 組の Started / Ended として扱います。
- サンプル `LS_ACFSample_Blend` の Camera Cut Track は、スクリプトから `Can Blend` を ON にできません（UE の内部プロパティ）。UE 5.8 の実行時は Section の Ease In / Out をこのフラグに関係なく使うので再生には影響しませんが、Sequencer でこのアセットを編集する場合は Track の `Can Blend` を ON にしてください（OFF のまま Section を動かすと Ease が消えます）。
- Sequencer を Editor で開いて再生するプレビュー（PIE / ゲーム以外）は対象外です（Camera Cut がビューポートを直接動かし、PCM を使わないため）。

## 15. PlayerCameraManager の互換性（Phase 6B / B-2）

ACF は PCM の Camera Modifier として出力します。外部 ViewTarget（Sequencer のカメラなど）に書き込まないため、Modifier の呼び出しごとに「どの ViewTarget の POV か」を判定します。

### 判定の方法

| 順 | 条件 | 判定（`EACFViewTargetResolution`） | ACF の書き込み |
|---|---|---|---|
| 1 | PCM が `FACFScopedOutputViewTarget` で ViewTarget を伝えた | Explicit（正確） | その ViewTarget が外部でなければ書く |
| 2 | POV が `ViewTarget.POV` / `PendingViewTarget.POV` そのもの（標準 PCM） | POVAddress（正確） | 同上 |
| 3 | POV がコピーで、PCM に ViewTarget が 1 つだけ（`PendingViewTarget` なし） | SingleViewTarget | 同上 |
| 4 | POV がコピーで ViewTarget Blend 中。両方とも外部、または両方とも外部でない | SameDecision | 両方外部でなければ書く（どちらの呼び出しでも結論が同じ） |
| 5 | POV がコピーで、外部とゲームプレイの間の ViewTarget Blend 中 | Unresolved（判定できない） | **書かない**（外部カメラを上書きしないため）。PCM ごとに 1 回だけ警告 |

- 判定できないことを正常とは扱いません。5 の場合は推測で書かず、診断と警告で知らせます。出力を止めるのはこの場合だけで、ACF は裏で評価を続けます（復帰時に最新の構図）。
- 標準 PCM では常に 2 で判定され、Phase 5A までと動作は同じです。

### 対応表

| PCM | 対応 | 内容 |
|---|---|---|
| 標準 `APlayerCameraManager` とその派生（POV を参照のまま渡すもの） | 対応 | すべての機能。ViewTarget Blend 中も正確 |
| POV をコピーして渡す派生 PCM | 条件つき | ViewTarget が 1 つの間は正確（Camera Cut、通常の更新）。外部 ViewTarget との ViewTarget Blend 中は ACF が書かない（ゲームプレイ側に ACF が乗らないので、ブレンドの始まりと終わりで ACF 分の差が出る）。`FACFScopedOutputViewTarget` を使えば完全に対応 |
| `AGameplayCamerasPlayerCameraManager`（GameplayCameras プラグイン） | 条件つき | PendingViewTarget を使わない（ブレンドは GameplayCameras の内部）ので、常に 3 で判定され、Sequencer の Camera Cut 中は ACF が書かない。ただし下の制限がある |

**GameplayCameras の PCM の制限**（実際の PCM で自動テスト済みなのは Ease なしの Camera Cut だけ。以下の 2 点は UE 5.8 のソースからの判断で、実行では確認していません）
- ACF は GameplayCameras のカメラシステムの評価の後に Modifier として乗ります。GameplayCameras 内部のブレンド（Ease In / Out を含む）の間も ViewTarget は行き先に切り替わっているので、ACF の出力は切り替わった瞬間に付く / 外れます（ブレンドされません）。
- Sequencer の Ease Out は ACF からはカットに見えるため、ACF の Return Blend が GameplayCameras のブレンドに重なります。この PCM では Return Blend を 0（カット）にしてください（`Set ACF Sequencer Options` の Return Blend）。
- ACF の機能を GameplayCameras のカメラリグとして組み込む（Camera Output Adapter）ことはしていません。

### 導入方法（POV をコピーする独自の PCM）

`ApplyCameraModifiers` を呼ぶ間、その呼び出しがどの ViewTarget のものかを ACF に伝えます。GameplayCameras など他のプラグインへの依存はありません。

```cpp
#include "ACFCameraSubsystem.h"

void AMyCameraManager::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
    FACFScopedOutputViewTarget Scope(this, ViewTargetOfThisPass); // この呼び出しの ViewTarget
    Super::ApplyCameraModifiers(DeltaTime, InOutPOV);
}
```

- PCM の LocalPlayer に ACF のサブシステムが無ければ何もしません。Blueprint の PCM からは使えません（C++ のみ）。

### 診断

- `Get ACF Camera Output Diagnostics`（`UACFCameraSubsystem::GetOutputDiagnostics`）: PCM のクラス、最後の判定方法、最後のフレームの Modifier の呼び出し回数と ACF が書き込んだ回数、POV がコピーされたか、判定できなかった回数（PCM ごと）。
- `showdebug camera`: 上の内容を 1 行で表示します（判定できなかった回数があるとオレンジ）。
- ログ: POV のコピーを初めて検出したとき（PCM ごと 1 回、Log）、判定できないブレンドを初めて検出したとき（PCM ごと 1 回、Warning）。毎フレームは出しません。

### 検証

- 自動テスト `.Sequencer.PcmCompat.*`（7 件、`-game`）: POV をコピーする最小の派生 PCM（Camera Cut と ViewTarget Blend）、PendingViewTarget を使わない派生 PCM、連携 API を使う派生 PCM、実際の `AGameplayCamerasPlayerCameraManager`（プラグインが無効ならスキップ）、標準 PCM（判定と出力が従来どおり）、ローカルプレイヤー 2 人（PCM の種類が違っても診断が混ざらない）。どれも画面の最終的な位置・FOV と、ACF の書き込み回数を確認します。

## 16. 不正値への対策（Phase 6B / B-4）

NaN・Infinity・不正な FOV・不正な DeltaTime などが入っても、ACF の内部状態（Damping、ブレンド、Node の前フレームの状態）と PCM への出力に残らないようにしています。**正常な入力では、出力は修正前とビット単位で同じです**（自動テストで確認）。

### どこで止めるか

| 入口 | 検査 | 不正だったときの扱い |
|---|---|---|
| 対象の Transform（`FACFCameraTarget::Resolve`。全 Node の共通の入口） | 位置・回転・スケールが有限で、回転が正規化できる | そのフレームは「解決できない」扱い。対象が存在する間は、追従系（Follow / Orbit / Lock-On / Composer）が最後の正常な Transform を保持する。存在するので「対象の喪失」（自動解除）にはならない |
| 計算された追従点（グループの中心、ロック点） | 同上 | 最後の正常な点を保持 |
| 評価の文脈（Director） | DeltaTime が有限で 0 以上 / UE カメラ（Upstream）の姿勢 / ControlRotation | DeltaTime は 0 秒として評価（時間を進めない・戻さない）。UE カメラは最後の正常な値、ControlRotation は最後の正常な値（無ければ「無し」） |
| 実行時の入力 | カメラ入力・入力レート・Orbit の回転・`Blend ACF Camera From Pose` の姿勢と時間・出力抑制のブレンド時間 | 入力を拒否（`Blend From Pose` は false を返す） |
| ブレンド時間（Preset / 要求 / Sequence） | 有限 | NaN / Inf はカット（そのままでは遷移が終わらない） |
| Node の出力（`RigInstance`。Node ごと） | 正常な姿勢を不正にした Node | 不正になった成分（位置・回転・FOV）だけ Node の前の値に戻す。位置か回転なら、その Node の補間状態をリセット（Damping に NaN が残ると二度と戻らないため。副作用: その Node の Damping が次のフレームで目標に一度スナップする） |
| 演出（Effect Stack） | 演出を足した姿勢 | そのフレームの演出を捨てる（演出そのものは止めない） |
| 最終の姿勢（PCM に渡す直前） | 位置・回転が有限、FOV が有効 | 最後の正常な姿勢（同じ World・PlayerController・ViewTarget の中だけ）→ 無ければ UE カメラ → それも無ければ **ACF は出力しない（Weight 0）** |
| Output Modifier | 書き込む姿勢を再確認 / ACF の下の PCM の姿勢 | 不正な姿勢は書かない。PCM の姿勢が不正で Weight < 1 なら、ACF の姿勢を 100% で書く（混ぜると不正値が画面に出るため） |
| Collision Guard | 出力位置と拘束の中心が有限 | Guard を掛けない（PCM の姿勢はそのまま） |

- **FOV の有効範囲**: 有限で 0 より大きく 180 未満。UE 5.8 は投影を `Max(0.001, FOV)` から作り（`CameraStackTypes.cpp`）、180 度以上では投影が成り立たないためです。0 < FOV < 0.001 は UE と同じく 0.001 に揃えます。170 度など大きいが有効な値はそのまま通します。
- **最後の正常な姿勢（Fallback）**: ローカルプレイヤーごとに 1 つ。World・PlayerController・PCM の ViewTarget が変わると捨てます（マップ移動の前の姿勢を使わない）。UE カメラも不正で Fallback も無いフレームは、Rig を中立の姿勢から評価して状態だけ進め、出力も記録もしません。
- **外部 ViewTarget**（Sequencer のカメラなど）: ACF が書かない呼び出しでは、何も検査・補正しません（外部カメラの FOV 175 などもそのまま）。B-2 の判定は変えていません。

### 診断

- `Get ACF Camera Health Diagnostics`（`UACFCameraSubsystem::GetHealthDiagnostics`）: 種類別の回数（対象、DeltaTime、UE カメラ、拒否した入力、Node の出力、演出、最終の姿勢）と、最後の問題（種類・Node のクラス・要求の Handle・パラメータ・Fallback・フレーム）。
- ログ: `LogActionCamera` の Warning を、種類ごとに 1・10・100・1000 回目に出します（毎フレームは出さない。再発は回数で分かる）。
- `ACF.Debug.Health`: 全ローカルプレイヤーの回数と最後の問題をログに出します。`showdebug camera` にも 1 行出ます（問題があるときだけ）。
- Shipping では回数と診断は動きますが、ログは UE の設定に従います（既定では出ません）。

### 対応していない・限定的な入力（制限）

- 対象を直接読む Node（Collision の視線、Combat、Shot Anchor、Group Framing のメンバー）は、不正な Transform のフレームでは対象が「無い」ものとして動きます（保持はしない）。グループのメンバーはそのフレームだけ外れます。Lock-On のロック対象は、既存の仕組みで最後に分かっているロック点を保持します。
- 有限だが意味のない Node のパラメータ（負の Damping、極端に大きい Offset など）は検査しません（各 Node の既存の扱いのまま）。
- 非有限のパラメータを持つ演出は、終わるまで毎フレーム捨てられます（自動では止めません。回数で分かります）。
- Collision のパラメータ（Probe 半径など）が非有限の場合、Collision Node の出力が捨てられますが、エンジンの問い合わせ自体は行われます。
- 出力しないフレーム（Fallback 無し）の `Get Aim Camera Pose` は中立の姿勢です。評価結果の Weight が 0 であることで判別してください。
- 登録済みの Scene Component に NaN の Transform を設定すると、UE 自身が ensure を出します（UE の診断。ACF の外）。

## 17. Actor の参照と寿命（Phase 6B / B-3）

ACF は World の Actor を所有しません。ローカルプレイヤーの ACF（サブシステムと Director）はマップ移動をまたいで生き続けるので、World の Actor を強参照すると、その Actor と World 全体を残してしまいます。

### 参照の種類

| 保持しているもの | 参照 |
|---|---|
| 追従・注視の対象、Target Group のメンバー、Instigator、Sequence の攻撃者・対象、演出の発生源、出力 Guard の無視リスト、Sequencer 連携の ViewTarget | 弱参照（`TWeakObjectPtr`） |
| `Collision Ignored Actors`（要求のパラメータ） | Blueprint / C++ から渡すときは Actor の配列のまま。**Director が受け取った時点で弱参照（`IgnoredActorRefs`）へ移し、強参照は残しません**（Blueprint は弱参照の配列を扱えないため、入力の型は変えていません） |
| 1 フレームの評価の中だけで使うもの（Collision の無視リスト、Shot の Probe、演出の Motion Target など） | 生ポインタ。フレームをまたいで保持しない |
| Deoccluder の遮蔽状態 | 対象を解決した時点で記録したオブジェクトの識別子（`FObjectKey` = 番号 + シリアル番号）。参照はせず、比較だけに使う（Actor の無い対象は従来どおり枠の番号） |
| `Get ACF Sequencer Camera State` の戻り値の ViewTarget | 呼び出し側に返す値（ACF は保持しない） |

- UE の GC には「Garbage になったオブジェクトへの強参照を null にする」機能（`gc.GarbageEliminationEnabled`）がありますが、ini や `-DisableGarbageElimination` で無効にでき、Compact Set を既定にしたエンジンでは常に無効です。ACF はこの設定に依存しません（自動テストは両方の設定で実行）。
- Deoccluder の遮蔽状態は、以前は Actor のアドレスで照合していました。破棄された Actor のメモリに新しい Actor が確保されると、遮蔽の確定（0.25 秒の待ち）を引き継ぐことがありました。識別子は再利用されないので、新しい Actor は必ず待ちからやり直します。

### 確認していること（自動テスト `.Lifetime.*`、実際の World・Actor・GC）

- 無視リストの Actor を Director が強参照していない（`FReferencerFinder`）。破棄して GC するとメモリが解放される。その後も Collision の評価は続き、残った Actor だけを無視する。
- 要求と Finisher を出したまま World を破棄（マップ移動・PIE 終了と同じ）すると、World と Actor が解放される。Director はその後も評価を続ける。
- Deoccluder の状態は、同じアドレスに確保された新しい Actor に引き継がれない。
- 2 つの Director（2 人のローカルプレイヤー）の無視リストと破棄の影響は混ざらない。正常な Collision の結果は修正前とビット単位で同じ。
- PlayerController / PCM の作り直し、ローカルプレイヤーの追加・削除、Sequencer の終了は既存のテスト（`.Sequencer.*`、`.Integration.*`）で確認しています。

### 確認していないこと（制限）

- Level Streaming のアンロードは、テスト用の Streaming Level が無いため実行していません。アンロードされたレベルの Actor は破棄と同じ扱い（弱参照が解決できなくなる）になるはずですが、未確認です。
- 実際のマップ移動（`open`）と PIE の終了は、テスト用 World の破棄で代用しています。
- 古い型（`TObjectPtr` の配列）でこの構造体を保存したアセットは、このプロジェクトにはありません。入力の型を変えていないので、保存済みのデータもそのまま読めます。

## 18. ACF カメラが無いときの演出（Phase 6B / B-5）

### 仕様

- 演出は ACF カメラの上にかかります。**ACF カメラが無いとき、ゲームからの演出の再生（`Play ACF Camera Effect` / `Impulse` / `Recoil` / `Shake`）は拒否します。**
  - 「ACF カメラがある」= カメラ要求が 1 つ以上ある、または最後の要求がブレンドで UE カメラへ戻っている途中（`Can Play ACF Camera Effects`）。最後の要求がカットで外れた直後は、同じフレームでも「無い」です。
  - 拒否した演出は保持しません（予約もしない）。戻り値の Handle は無効で、`Is ACF Camera Effect Active` も false です。拒否は回数を数え（`GetNumRejectedCameraEffects`）、1・10・100・1000 回目… に `LogActionCamera` の Warning を出します。
- 修正前は、要求が無い状態で再生した演出が止まったまま溜まり、何秒も後に最初の要求を出した瞬間に再生されていました（自動テストで再現）。
- **受け付けた演出の寿命**: 要求が途中で無くなっても、演出は自分の長さで進んで終わります（出力の Weight は 0 なので画面は UE カメラのまま）。そのために、演出が残っている間は出力 Modifier を接続したまま評価を続けます（PCM が作り直されても）。
- **World が変わると止まる**: ACF はマップ移動をまたいで残るので、前の World で再生した演出（無限に続く演出を含む）は、次の World で最初に評価する前にすべて止めます。要求そのものはローカルプレイヤーのものなので残ります。
- 変えていないもの:
  - 要求の切り替え中・Sequencer のカメラが映っている間（要求は裏で生きている）・一時停止中の再生は、従来どおり受け付けます。一時停止中は演出の時間も止まり、解除すると始まります。
  - Finisher / Camera Sequence が持つ演出は、Sequence の要求の上で再生するので、この制限の対象外です（従来どおり Shot / Sequence の寿命で止まる）。
  - Director の C++ API（`UACFCameraDirector::PlayEffect`）は内部用で、判定しません。

### 方式の比較

| 方式 | 内容 | 判断 |
|---|---|---|
| A（採用） | ACF カメラが無ければ開始を拒否する | 過去の演出が後から再生されることが無い。演出は ACF カメラにかかるという既存の契約どおり。拒否は Handle と `Can Play` で分かる |
| B | 要求が無くても時間を進め、終わったら捨てる | 画面に出ない演出を進めるだけで、受け付けたのに映らない。一度も要求の無いプレイヤーにも Modifier を付けることになる |
| C | 予約した演出だけを後から再生する | 新しい API と、予約の有効期限・所有者の仕様が必要。今の使い方に必要な場面が無い |

受け付けた後で要求が無くなった演出には、B と同じ扱い（時間を進めて終わらせる）を残しています。

### 制限

- ゲームが「要求を出す前に演出を再生」していた場合、その演出は再生されなくなります（要求を先に出すか、素通し Preset を要求してください）。
- PCM がまだ無いとき（ゲーム開始直後など）に要求と演出を出した場合、両方とも PCM ができた時点から始まります（要求と同じ扱い）。
- 自動テスト: `.Sequencer.Effects.*`（5 件、`-game`）と `.Lifetime.EffectsNeedAnAcfCameraAndEndWithTheWorld`。

## 19. Sequencer の通知のタイミング（Phase 6B / B-6）

`On Sequencer Camera Started` / `On Sequencer Camera Ended` は、**PlayerCameraManager のカメラ更新の中では配信しません**。ハンドラの中で `SetViewTarget` / `SetViewTargetWithBlend`、Sequence の停止・再生、ACF の要求の解除などを呼んでも、更新中の PCM の状態を書き換えることはありません。

### 配信のタイミング

状態の変化は検出した時点で確定し、通知はキューに入れて、次の 2 か所でだけ配信します。どちらも UE 5.8 の World の Tick（`LevelTick`）の中で、カメラ更新のループ（`UpdateCameraManager`）とは別に呼ばれるので、どの経路で変化が起きても（World の Tick、ACF の評価前フック、ゲームが手で呼んだ `UpdateCamera`、`RefreshNow`）、ハンドラがカメラ更新の中で動くことはありません。

| 配信点 | 時点 | 配信するもの |
|---|---|---|
| Sequencer 連携のサブシステムの Tick（`TickObjects`） | カメラ更新より前 | 前のフレームから残っているもの、続けてこの Tick で検出した変化 |
| `OnWorldPostActorTick` | 同じフレームのカメラ更新の後 | カメラ更新の直前（ACF の評価前フック）で検出した変化など |

- 通常は World の Tick で検出するので、通知は従来どおり同じフレームのカメラ更新より前に届きます。Tick の後に別のシステムが Sequence を止めた場合は、従来は**カメラ更新の途中**で届いていましたが、今は**同じフレームのカメラ更新の後**に届きます。カメラ更新が `OnWorldPostActorTick` より後に（ゲームのコードから）行われた場合は、次のフレームの Tick で届きます。
- `RefreshNow` は状態をその場で確定しますが、**通知はその場では届きません**（次の配信点で届く）。状態は `Get ACF Sequencer Camera State` ですぐに読めます。
- 一時停止中もサブシステムは配信のためだけに Tick します（状態の判定は従来どおり一時停止中は行いません）。一時停止中にキューに入った通知も届きます。
- 変化の検出と ACF の内部処理（出力の抑制、外部 ViewTarget の登録、ACF の Return Blend の開始）は、従来どおり検出した時点で行います。**カメラの結果（Camera Cut、Return Blend）は変わりません。**
- 順序は検出した順（開始 → 終了）で、1 つの変化につき 1 回です。`RefreshNow` を何度呼んでも二重には届きません。
- 1 回の配信は 64 件までです。残りは次の配信点（同じフレーム内の次、遅くとも次のフレームの Tick）で、新しい変化が無くても必ず届きます。ハンドラの中でさらに通知が積まれても再帰はせず、同じ規則で順に届きます。
- 通知の引数（PlayerController、Sequence Actor、Return Kind）は検出した時点のものです。開始と終了が同じフレームで検出された場合、開始のハンドラの時点で状態（`Get ACF Sequencer Camera State`）はすでに終了後になっていることがあります。通知の中では、状態の問い合わせより引数を使ってください。
- 配信しないもの: 対象のローカルプレイヤーが削除されたイベント、別の World のイベント。World の破棄（サブシステムの終了）で未配信のイベントは捨てます。PlayerController や Sequence Actor が配信までに破棄された場合は、その引数を null にして配信します（開始と終了の組を崩さないため）。破棄・GC されたリスナーは呼ばれません（Dynamic Delegate の規則）。
- キューは 4 件まで固定領域でヒープを確保しません。通知の無いフレームのコストは、配信点ごとの空判定だけです。

### 確認していること（自動テスト `.Sequencer.Events.*`、6 件、`-game`）

- カメラ更新の直前に Sequence が止められても、通知はカメラ更新の外で、同じフレームに届く。Return Blend は従来どおり。
- 通知の中で `SetViewTargetWithBlend` を呼ぶ／開始の通知で Sequence を止め終了の通知で要求を解除しても、最終的な ViewTarget・画面・ACF の状態、回数、順序が正しい。
- 150 件を一度に積んでも、新しい変化なしに全件が順に届く。ハンドラが通知のたびに次を積む連鎖（200 件）も、再帰せずに全件届く。
- `RefreshNow` は通知を同期では出さず、3 回呼んでも 1 回だけ届く。一時停止中も届く。同じフレームの開始と終了は、それぞれ検出時の引数で届く。
- 削除されたローカルプレイヤー宛ての通知は届かない。GC されたリスナーは呼ばれない。未配信の通知を持ったまま World を破棄しても配信されない。
- 修正前は、カメラ更新の直前に止めたケースなど 3 件で、通知がカメラ更新の途中で届いていました（クラッシュや状態の破綻は再現していません）。

### 確認していないこと

- 「World の破棄で未配信を捨てる」は、ローカルプレイヤーのいないテスト用 World（`FTestWorldWrapper`）で確認しました。実際のマップ移動の最中は確認していません。

## 20. Blueprint からの戻りのブレンド（Phase 6B / B-7）

ゲーム独自のカメラや演出から ACF カメラへ滑らかに戻すための Blueprint ノードです。補間は Director の既存の戻りのブレンド（Sequencer の Return Blend と同じもの）に任せ、ノードは対象のプレイヤーの特定と実行条件の確認だけを行います。

### ノード（Blueprint Library、カテゴリ Action Camera | Return Blend）

| ノード | 引数 | 戻り値 |
|---|---|---|
| `Blend ACF Camera From Pose For Player` | PlayerController、Pose（`ACF Camera Pose`）、Blend Time（秒）、Curve | 成功したか（bool）と Result |
| `Blend ACF Camera From Current View For Player` | PlayerController、Blend Time、Curve | 同上。Pose は直前に描画された画面（PCM のカメラキャッシュ） |
| `Release Camera With Return Blend For Player` | PlayerController、Handle、Blend Time、Curve | 解除できたか。画面に出ている要求なら、戻り先（下の要求でも UE カメラでも）へこのブレンドで戻る |

- 成功は Result = `Started`（次のカメラ更新からブレンド開始）または `Cut`（Blend Time 0 / Curve = Cut。ブレンドせずに ACF カメラをそのまま表示）。
- それ以外は何もせず false で、Result が理由です。

| Result | 意味 | 対処 |
|---|---|---|
| `NoPlayer` | PlayerController / ACF のサブシステムが無い | 対象のプレイヤーを確認 |
| `NoCameraManager` | PlayerCameraManager がまだ無い（後で始まると古い姿勢から動くので拒否） | PCM ができてから呼ぶ |
| `NoCameraRequest` | ACF の要求が無い（戻る先の ACF カメラが無い） | 先に要求を出す |
| `CameraManagerBlending` | PCM が `SetViewTargetWithBlend` などで ViewTarget をブレンド中 | PCM のブレンドだけで戻す（二重にしない。Sequencer 連携と同じ規則） |
| `ExternalViewTargetActive` | 画面に外部 ViewTarget（ACF が出力しないカメラ）が出ている | 先にゲームプレイの ViewTarget へカットで戻す |
| `InvalidPose` | 姿勢が非有限、または FOV が無効（16 章の診断に数える） | 正しい姿勢を渡す |
| `InvalidBlendTime` | Blend Time が NaN / 無限 / 負 | 0 以上の有限値 |

- ブレンド中にもう一度呼ぶと、新しい姿勢からやり直します。最後の呼び出しが優先です（Sequencer の終了と同じフレームに呼んだ場合も、後から呼んだ方）。
- プレイヤーごとに独立です（他のローカルプレイヤーには影響しない）。姿勢はワールド座標なので、同じ World のものを渡してください。
- C++ では `UACFCameraSubsystem::TryBlendFromPose` / `TryBlendFromCurrentView` / `ReleaseCameraWithReturnBlend`。既存の `Blend ACF Camera From Pose`（サブシステムのノード）は従来どおりで、条件の確認はしません。

### 使用例: ゲーム独自の Camera Actor で演出し、ACF カメラへ戻る

1. 演出の開始: `Add ACF External View Target`（Camera Actor、Source = 自分）→ `Set View Target`（Camera Actor、Blend Time 0 = カット）。登録したカメラには ACF が乗らないので、Camera Actor の姿勢がそのまま映ります（ACF は裏で評価を続けます）。
2. 演出の間: Camera Actor を自由に動かします。
3. 戻す（同じフレームで続けて）: `Set View Target`（Pawn など元の ViewTarget、Blend Time 0 = **カット**）→ `Blend ACF Camera From Current View For Player`（Blend Time 0.5 など）→ `Remove ACF External View Target`。
   - カットで戻すのは、ブレンドを ACF の 1 本にするためです。`Set View Target with Blend` で戻す場合は PCM のブレンドが画面を動かすので、このノードは呼びません（呼ぶと `CameraManagerBlending` で拒否されます）。
   - `Current View` は「直前に描画された画面」なので、カットで戻したのと同じフレームに呼ぶと、演出カメラの最後の画面から ACF カメラへ補間されます。自分で姿勢を作る場合は `Blend ACF Camera From Pose For Player`（例: Camera Actor の位置・回転・FOV）。
   - 外部 ViewTarget の登録は、カットで戻した後に外して構いません（戻した時点で画面には出ていない）。
4. 結果を見る: false なら Result で理由が分かります。

**Sequencer を使う場合との違い**: Level Sequence の Camera Cut は、Sequencer 連携（14 章）が外部 ViewTarget の登録と戻りのブレンドを自動で行います。戻り方は `Set ACF Sequencer Options` の Return Blend で決まり、このノードは不要です。終了の通知（`On Sequencer Camera Ended`、19 章。カメラ更新の外で届く）の中から呼んで、Sequencer がカットで戻した後の戻り方をゲーム側で決めることもできます。

### 確認していること（自動テスト `.Sequencer.ReturnBlend.*`、5 件、`-game`）

- Blueprint のノードと C++ の既存 `BlendFromPose` で、毎フレームの画面の位置が一致する。
- Blend Time 0（Cut）、NaN / 無限 / 負、NaN の姿勢、FOV 0 の姿勢、要求なし、PCM なし（コントローラーの無いローカルプレイヤー）、プレイヤーなし、PCM のブレンド中、外部 ViewTarget の表示中。拒否したときは画面も ACF の状態も変わらず、無効な姿勢と時間は 16 章の診断に数えられる。
- ブレンド中の再呼び出し（新しい姿勢からやり直す）、2 人のローカルプレイヤーの独立。
- 上の使用例の手順そのもの（演出カメラの最後の画面から ACF カメラへ、1 フレームの移動量がブレンドの範囲内）。Sequencer がカットで戻った終了の通知の中から呼ぶ場合。
- `Release Camera With Return Blend For Player`: 下の要求へ戻るときにも指定のブレンドを使う（指定しない解除は従来どおり下の要求の Blend In）。

## 21. Preset の検証（Phase 6B / B-8）

Camera Preset の設定の誤りを、保存のとき・Validate Assets のときに見つけます。Editor 専用です（`WITH_EDITOR`。ゲームのビルドには入らず、実行中のカメラには何もしません）。Preset を書き換えることはありません（自動修正はしません）。実行中の不正値（NaN のターゲット、Node の出力など）は 16 章の健全性チェックの担当です。

### 使い方

- **保存のとき**: Preset を保存すると、Data Validation（UE 標準の DataValidation プラグイン。既定で有効）が自動で検証します。結果は Message Log の「Asset Validation」に出ます。
- **まとめて確認**: Content Browser で Preset（またはフォルダ）を右クリック → **Asset Actions → Validate Assets**。コマンドラインでは `-run=DataValidation`。
- **Camera Sequence**: Sequence を検証すると、ブレンドでつながる Shot どうしの注意（下の「遷移」）も出ます。
- **C++**: `ACFCameraPresetValidation::ValidatePreset(Preset, Issues)` が結果を配列で返します（`ACFCameraPresetValidation.h`。`ValidateTransition(From, To, Issues)`、`HasCollision(Preset)` も同じファイル）。Data Validation・Sequence・テストはすべてこの 1 つの実装を使います。

1 件の結果は、重大度、ID（例 `ACF.Param.NonFinite`）、Preset、Node（番号とクラス）、項目名、説明、直し方を持ちます。メッセージは `[ID] Node 番号 (クラス) 項目: 説明 Fix: 直し方` の形です。

### 重大度

| 重大度 | 意味 | Data Validation |
|---|---|---|
| Error | この設定では動かない（NaN / Inf、UE が描画できない FOV） | エラー。アセットは Invalid |
| Warning | 動くが、意図した動きではない可能性が高い（役割の競合、範囲外の値、Min > Max など） | 警告。アセットは Valid のまま |
| Info | 許される設定だが知っておくとよいこと（Collision なし、評価順と並び順の違いなど） | 情報（Message Log に出るだけ） |

### 検出すること

**Node の構成**

| ID | 重大度 | 内容 |
|---|---|---|
| `ACF.Preset.NoNodes` | Warning | Node が 1 つもない（UE のカメラがそのまま映る） |
| `ACF.Preset.EmptyNodeSlot` | Warning | 空のスロット（評価で飛ばされる） |
| `ACF.Preset.DuplicateNodeReference` | Warning | 同じ Node オブジェクトが 2 つのスロットにある（1 フレームに 2 回評価される） |
| `ACF.Preset.BodyConflict` | Warning | Body の Node が同じ要素（位置・回転・FOV）をどれも上書きする。最後の Node だけが映る。Fixed Pose・Shot Anchor を含む。位置だけ・FOV だけのように**要素が重ならない組み合わせは報告しません**（例: Orbital Follow + FOV だけの Fixed Pose、位置用と FOV 用の 2 つの Fixed Pose） |
| `ACF.Preset.AimConflict` | Warning | Aim の Node（Look At / Rotation Composer）が 2 つ以上 |
| `ACF.Preset.ComposerAim` | Warning | Position Composer の後で Aim の Node が回転を変え、構図が崩れる |
| `ACF.Preset.DuplicateCombat` / `DuplicateGroupFraming` / `DuplicateCollision` | Warning | 同じ役割の Node が 2 つ以上（効果が重なる / 何度もスイープする） |
| `ACF.Preset.FramingNoSubjects` | Warning | 対象が 1 つも設定されていない Group Framing |
| `ACF.Preset.StageOrderClamped` | Warning | 独自 Node の `GetStageOrder()` が ±400000 を超え、丸められる |
| `ACF.Preset.RotationOverriddenByAim` | Info | Body の Node が回転も決めているが、後の Look At が置き換える（Rotation Composer は受け取った回転を基に補正するので対象外） |
| `ACF.Preset.EvaluationOrder` | Info | リストの並びと評価順が違う（評価は段の順。リストの順は同じ段の中だけで効く） |

無効（`bEnabled` OFF）の Node は構成の判定に数えません（調整中の比較用に置いた Node で警告しない）。

**パラメーター**（すべての Node と Preset の Default Blend In / Out の値を、プロパティの定義から調べます。範囲は各プロパティの `ClampMin` / `ClampMax`、つまり Details パネルが許す範囲で、検証のための独自の上限はありません）

| ID | 重大度 | 内容 |
|---|---|---|
| `ACF.Param.NonFinite` | Error | NaN / Inf。無効な Node や OFF のオプションの値は Warning（今は使われないが、ON にすると壊れる） |
| `ACF.Param.InvalidFov` | Error | FOV が 0 以下、または 180 以上（UE の透視投影で使えない）。Fixed Pose / Shot Anchor の FOV は Details パネルで 360 まで入力できるので、ここで止めます |
| `ACF.Param.OutOfRange` | Warning | `ClampMin` / `ClampMax` の範囲外（C++・Blueprint・Python・古いアセットから入った値）。負の Damping（Damping なしと同じ）、負の Collision の半径（UE はライントレースにする）、負のブレンド時間（カット）などは、実際の動きも説明に出ます |
| `ACF.Param.MinMaxInverted` | Warning | Min > Max（Orbital Follow / Lock-On の Pitch、Group Framing の FOV）。実行時に入れ替えて使われる |
| `ACF.Param.RangeInverted` | Warning | Near Range ≥ Far Range（Lock-On: 距離で滑らかに変わらず Far Range で切り替わる、Combat: 常に遠い側の値） |
| `ACF.Param.ZeroDistance` | Warning | カメラが中心に重なる（Orbital Follow の Distance 0、Lock-On の腕の長さが両方 0、Aim At Anchor の Shot Anchor の Camera Offset 0） |
| `ACF.Param.Normalized` | Info | 実行時に補正される値（Soft Zone < Dead Zone、Pitch の範囲外の Initial Pitch / Base Pitch） |
| `ACF.Blend.MissingCurve` | Warning | Custom のブレンドに Curve がない（Linear になる） |

機能に依存する値は、その機能が ON のときだけ範囲を調べます（例: `Override Field Of View` が OFF の FOV、Curve = Cut のブレンドの時間、`Use Rings` が OFF のリングの距離）。判定にはプロパティの `EditCondition`（`bFlag` / `!bFlag` / `Enum == 値` / `Enum != 値` と `||` / `&&`）を使い、読めない条件は「ON」とみなします（調べすぎる方に倒す）。

**Collision**

- `ACF.Collision.None`（Info）: ターゲットの周りにカメラを置く Node（Follow / Orbital Follow / Lock-On / Position Composer / Shot Anchor）があるのに Collision がない。壁に入ることがあり、出力の Guard（16 章）もこのカメラには効きません。固定カメラ、ズームだけの Preset、壁に近づかないカメラなど、**Collision なしは正しい構成**なので Warning にはしません。Fixed Pose だけの Preset には何も出しません。

### 遷移（2 つの Preset の間のブレンド）

ブレンド中は、入ってくる側（ブレンド先）の Preset の PostBlend（Collision）が、ブレンドされた姿勢に最初のフレームから使われます（Phase 6A 監査 P-3）。そのため、どちらの Preset からどちらへブレンドするかが分からないと判断できない注意は、単体の Preset の検証では出しません。

| ID | 重大度 | 内容 |
|---|---|---|
| `ACF.Transition.CollisionDropped` | Warning | Collision あり → なし。ブレンドの最初から Collision も出力の Guard も効かず、ブレンドの途中で壁を通ることがある |
| `ACF.Transition.CollisionAdded` | Info | Collision なし → あり。ブレンド先の Collision がブレンドの最初から効く |
| `ACF.Transition.CollisionSettingsDiffer` | Info | どちらも Collision があるが、チャンネルか半径が違う。ブレンドの間ずっとブレンド先の設定が使われる |

- Camera Sequence の検証は、ブレンドでつながる連続した Shot（Blend In がカットでないもの）にこれを使い、Info として出します（Collision のない Shot の警告は従来どおり別に出ます）。
- ゲームの中でどの Preset からどの Preset へ切り替えるかはアセットからは分からないので、それ以外の遷移は `ValidateTransition` を自分で呼んで確認してください。
- Collision の有無をまとめて決める「Collision Policy」のような設定は、将来の候補です（今回は追加していません）。

### 実行時

- 実行時には検証しません。毎フレームの処理も、要求の開始時の処理も追加していません（16 章の健全性チェックが実行時の値を扱います）。
- 範囲の情報（`ClampMin` などのメタデータ）は Editor にしかないので、パッケージしたゲームでは同じ検証はできません。
- Preset を一時的に作って使う場合（実行中の調整用の Preset など）も、検証で止められることはありません。保存しない Preset は自動では検証されないので、必要なら `ValidatePreset` を呼びます（Editor のビルドのみ）。

### 確認していること（自動テスト）

- `.PresetValidation.*`（12 件。Editor とゲームの両方）: 正しい Preset で何も出ないこと（意図的な分担・無効にした重複を含む）、Fixed Pose と Shot Anchor の競合、同じ段の問題と評価順、空のスロットと同じ Node の重複（落ちない）、NaN / Inf、負の Damping と負の半径、不正な FOV、Min > Max、Collision なしの Preset が通ること、Collision の設定が違う Preset の遷移（Camera Sequence を含む）、検証で Preset のバイト列が変わらないこと、検証を挟んでもカメラの姿勢がビット単位で同じこと。
- `.Editor.*`（2 件。Editor のみ）: DataValidation の `UEditorValidatorSubsystem` を通した検証（保存時と手動の両方の用途。エラーで Invalid、警告と Info は Valid のまま）。プロジェクトとプラグインの実際の Preset・Camera Sequence をすべて検証し、エラーが 0 件であること（現在のサンプルは Error 0 / Warning 0。Info は検証用の Collision のない Preset 1 件）。

### 制限

- 組み込みの Node の役割（どの要素を上書きするか）は、検証側の表で判断しています。独自の Node は役割の競合の判定に入りません（パラメーターの NaN / 範囲は独自の Node も調べます）。
- 値の組み合わせのうち、実行時のターゲットや入力で決まるもの（ターゲットがいるか、Lock-On の対象があるか）は調べられません。
- Info は保存のときにも Message Log に出ます。

## 22. テストの実行とテスト環境（Phase 6B / B-9）

### 実行方法（`Scripts/RunTests.sh`、Git Bash）

プラグインのフォルダーで実行します。プロジェクト（このプラグインより上にある最初の `.uproject`）とエンジン（`$UE_ROOT`、無ければ `C:/Program Files/Epic Games/UE_5.8`）は自動で探します。`--project` / `--engine` で指定もできます。

| 目的 | コマンド |
|---|---|
| ACF の全テスト（ゲーム、ACF のテスト環境） | `Scripts/RunTests.sh` |
| 特定のカテゴリだけ | `Scripts/RunTests.sh "ActionCameraFramework.Sequencer+ActionCameraFramework.Authority"` |
| Editor コンテキスト | `Scripts/RunTests.sh --context editor` |
| 描画ありのサンプル（テストマップ） | `Scripts/RunTests.sh --context samples` |
| 性能の基準値（8 章） | `Scripts/RunTests.sh --context profile` |
| 繰り返し（毎回新しいプロセス） | `Scripts/RunTests.sh --repeat 10 ActionCameraFramework.Integration.SpeedFovAndRoll` |
| 1 件ずつ別のプロセスで（順序・状態への依存の確認） | `Scripts/RunTests.sh --isolate ActionCameraFramework.Integration` |
| ホストプロジェクトの既定のマップと GameMode で（B-9 より前の方法） | `Scripts/RunTests.sh --context host` |
| 対象のテスト名の一覧 | `Scripts/RunTests.sh --list ActionCameraFramework.Sequencer` |

- 合否は、テストの結果だけでなく次のすべてで判定します: エディターの終了コードが 0、ログの「N tests performed」の N が 1 以上、N 件すべてが `Result={Success}`、失敗 0 件、クラッシュ・ensure のログなし。1 回でも不合格なら終了コード 1 です。
- 結果は `<project>/Saved/Logs/ACFTests/<context>/` に、実行ごとのログと `summary.tsv`（実行・結果・テスト名の 1 行 1 件）で残ります。
- 自分をスキップするテスト（テストマップ以外の `.Samples.*`、`-ACFProfile` なしの `.Profile.Scenarios`、GameplayCameras が無いときの互換テスト）も、UE の集計では Success になります。判定の行に、そのうち何件がスキップだったかを表示します。
- アセットの作り直しは `Scripts/RunEditorPython.sh Scripts/CreateVerificationAssets.py` / `Scripts/CreateManualTestAssets.py`（どちらも何度実行しても同じ構成になるように、既存のアセットを再利用します）。

### ACF のテスト環境（既定の `game` コンテキスト）

- マップはエンジンの空のマップ `/Engine/Maps/Entry`、GameMode はエンジンの `GameModeBase`（URL の `?game=`）、GameInstance はエンジンの `GameInstance`（`-ini:` で上書き）です。PlayerController・Pawn（DefaultPawn）・PCM もエンジン標準なので、ホストプロジェクトのゲームの処理（REBoarGetch の `BP_BoarGameInstance`、`BP_PC_Lobby`、ThirdPerson の Character）は動きません。新しいアセットは追加していません。
- 統合テストで使う Actor・ターゲット・SpringArm・壁は、各テストが自分でスポーンします（`OrbitMatchesSpringArm` は B-9 から自分の SpringArm を使い、ホストの Pawn の設定に依存しません）。
- `.Samples.*` と `.Profile.Scenarios` だけは、テストマップ `L_ACFCameraTest` とホストプロジェクトの GameMode（人型の Pawn）を使います（画面内の判定が人型の大きさを前提にしているため。既存のマップとアセットは作り直していません）。

### シナリオの後始末（`Public/Tests/ACFCameraTestEnvironment.h`）

実際の World・PlayerController・PCM を使う Integration / Sequencer のシナリオは、開始時に次を記録し、終了時（途中で失敗・中断した場合も）に戻します。テストが戻し忘れた場合は、そのテストのエラーとして名前を出します。

- すべての `ACF.` コンソール変数、ACF の設定オブジェクト（出力の Collision Guard など、Sequencer の設定）
- 固定タイムステップ、ゲーム World の Pause と Time Dilation
- プレイヤーの Control Rotation（Integration）
- 終了時に止めるもの: 要求、Camera Sequence、演出（B-9 より前は演出が次のテストまで残っていた）、Sequencer が後始末で出した通知（リスナーを外した後に配信し終えてから次へ）
- 残っていればエラー: 出力抑制、画面に出ている外部 ViewTarget の登録、計測中の Profiler
- 開始時に、そのプレイヤーの健全性の診断（16 章）を 0 に戻します。前のテストの報告で「N 件以上報告された」という判定が通ってしまうのを防ぐためです。

ヘッダーは `WITH_DEV_AUTOMATION_TESTS` の中だけで、通常の Tick には何も追加していません（Shipping のバイナリにテストの文字列が無いことを確認済み）。

### ホストのエラーログの扱い

2 人目のローカルプレイヤーを作るテストは、ホストの Blueprint の PlayerController が `BeginPlay` で Widget を作ると UMG のエラー（`CreateWidget cannot be used on Player Controller with no attached player`）を受けます。B-9 から、このメッセージを予期するのは「GameMode の PlayerController が Blueprint のとき」だけです。ACF のテスト環境ではエンジン標準の PlayerController なので何も無視せず、このエラーが出ればテストは失敗します（実際に 0 件）。

### `SpeedFovAndRoll` の不安定さ（原因と対策）

- 原因: テストがターゲットを Latent Command の中で動かしていました。Latent Command はエンジンの Tick（カメラの更新）の**後**に実行されるので、ターゲットは 1 フレーム遅れて「前のフレームの時間 × 速度」だけ動きます。Speed FOV / Roll は位置の差を**今のフレームの** DeltaTime で割って速度にします。フレーム時間が揃っていれば一致しますが、16.7 ms の後に 50 ms のフレームが来ると速度を 3 分の 1 と見積もり、Roll が 5 度から約 4.34 度に下がります（失敗した実行のログは 4.38 度）。
- 対策: ターゲットは World の Tick の中で自分で動きます（ProjectileMovement、そのフレームの DeltaTime × 速度。カメラの更新の前）。実際のキャラクターと同じ条件で、フレーム時間が揺れても速度は正確です。許容誤差は変えていません。
- 効果の計算自体は `.Effects.SpeedFovAndRollFixedStep` で固定 DeltaTime（30 / 60 / 144 Hz）で確認します。同じ時刻で同じ値、2 回実行してビット単位で同じ、上の 1 フレーム遅れ + 50 ms のフレームで 4.34 度に下がること（原因の再現）と、カメラと同じ時計で動かせば 5 度のままであること。
- `.Integration.SpeedFovAndRollRepeated.Run1〜3` は同じシナリオを同じプロセスで 3 回続けて実行し、前の実行の状態が残らないことを確認します。

### 確認した実行（B-9）

| 実行 | 結果 |
|---|---|
| `game`（ACF のテスト環境）全件 | 166 / 166 |
| `host`（REBoarGetch の既定のマップ・GameMode）全件 | 166 / 166（うち 7 件はスキップ） |
| `editor` 全件 | 91 / 91 |
| `samples`（描画あり） / `profile` | 6 / 6、1 / 1 |
| Integration・Sequencer・TestEnvironment・Lifetime・Profiler を 1 件ずつ別プロセス（`--isolate`） | 78 / 78 プロセス |
| カテゴリの組み合わせを変えた実行（Sequencer + TestEnvironment、Integration + Lifetime + Profiler） | 41 / 41、37 / 37 |
| `SpeedFovAndRoll`（+ 連続 3 回 + 固定ステップ）を 10 回、ホスト環境で 5 回 | 15 / 15 プロセス。Roll は 60 回すべて 5.00 |

### 確認していないこと（制限）

- UE の Automation は 1 プロセスで 1 回しか `RunTests` を受け付けず、カテゴリの中の順序は変えられません。順序への依存は「全件の連続実行」「1 件ずつ別プロセス」「カテゴリの組み合わせ」で確認しました。全件の順序をランダムに入れ替えた実行はしていません。
- PIE（エディターでのプレイ）の開始・終了をまたぐテストはありません（ゲームプロセスと `FTestWorldWrapper` の World の破棄で確認）。
- アセット生成スクリプトの再実行で結果が変わらないことは、コード（既存のアセットの再利用、マップの作り直し）からの判断です。B-9 では、追跡しているアセットを書き換えないよう再実行していません（実行スクリプトは読み取りだけのスクリプトで確認）。
- `.Samples.*` / `.Profile.Scenarios` はホストプロジェクトの GameMode に依存したままです。新しいプロジェクトでの実行は Phase 8 で確認します。
- 時間で待つ統合テスト（監査 T-2）は、まだ 40 箇所以上あります。B-9 では、不安定さが確認された `SpeedFovAndRoll` の原因だけを直しました。
