# Stage clear presentation (勝利演出)

## Flow
最後の捕獲 → `UCapturePresentationComponent` 完了 → `ABoarGameMode::HandleBoarCaptured` → `RefreshHousedBoarCount` → `RequestStageClear`
（捕獲演出が残っている間は保留）→ フレーム終端 `ResolveStageEnd` で Cleared 確定 → `SetStageActorsStopped(true)` → `RetryClearSave()` →
`OnStageCleared()`（BP） → `UStageClearPresentationComponent::StartPresentation()`（Clear専用・1回だけ）。

Component（`ABoarPlayerController::StageClearPresentation`）の流れ：
1. WaitingCapture：捕獲演出の残りを `CaptureWaitTimeout` まで待つ（超過時は捕獲演出を中断）。
2. Victory：HUD非表示、ステージBGMフェードアウト、`VictoryFocusPoint`（足元＋`VictoryFocusOffset`）を固定、遮蔽の少ない斜め前方に一時 `ACameraActor` を生成して `SetViewTargetWithBlend`。
   `MontageStartDelay` 後に `AM_Player_Victory_NetRaise`（FullBody Slot）を再生、最終ポーズでPause（消失まで掲げたまま）。Montage不可ならMeshを上下させるFallback。
   ジングル（BGMチャンネル）、ジャンプSE、`StarBurstTime` で網先端から星Niagaraを黄・白・シアン3色で生成、`GlowStartTime` からシアンOverlay、
   `DissolveStartTime` から身体と網（Playerに子アタッチされた全Actor）のMeshを同姿勢Proxy＋`M_CaptureDataTransfer` に置換して下→上に分解、キューブ粒子が上昇。
3. Retreat：`RetreatDuration` かけて `VictoryFocusPoint` を注視したまま `RetreatDistance` 後退・`RetreatHeight` 上昇・FOV `VictoryFOV`→`RetreatEndFOV`（`RetreatCurve` 未設定時Ease In/Out）。
   終盤 `ParticleFadeDuration` で星・粒子を消す。壁がある場合は開始時のSweepで後退量を縮める。
4. FadeOut：`StartCameraFade` で `FadeOutDuration` 暗転（保持）→ `CompleteStageEndPresentation()`。
   保存失敗時はGameMode側の既存ルールどおりResultへ進まない（`RetryClearSave` 成功時に表示）。
5. Result：`WBP_Result` が `Anim_StageClear`→（0.35s）`Anim_CaptureCount`→（0.35s）`Anim_SpecialCoin` を順に再生しSE。全完了＋`InputUnlockDelay` 後だけ任意入力でLobbyへ。

ActorはDestroyしない：元の Visibility / HiddenInGame / OverlayMaterial を保存し、Cancel/EndPlay で復元。Proxy・粒子・カメラはTransientで破棄。
GameOverは従来どおり（Componentは開始されない）。`SafetyTimeout` 超過・Player消失時は残りを省略してResultへ進む。

## Editor tuning
- `BP_BoarPlayerController > StageClearPresentation`：Timing / Camera / Animation / VFX / Audio（すべて秒・cm・度）。
- `BP_BoarGameMode > bUseStageClearPresentation`（OFFで従来の即Result）。`bWaitForStageEndPresentation` をBP演出に使う場合はそちらが優先。
- `WBP_Result`：`Anim_StageClear / Anim_CaptureCount / Anim_SpecialCoin`（UMGアニメ、Designerで編集可）、
  `RevealStepInterval`、`InputUnlockDelay`、`CaptureLineFormat`（{Captured}/{Target}）、`CoinLineFormat`（{Coins}）、`LateRevealWidgetNames`、各SE。

## Assets
- Animation：`/Game/Animation/Player/A_Player_Victory_NetRaise`（1.9s, 30fps, pelvis移動のみでジャンプ, Root Motionなし）、
  `/Game/Animation/Player/AM_Player_Victory_NetRaise`（FullBody, AutoBlendOut無効）。
- Blender：`SourceArt/StageClear/Player_Victory_NetRaise.blend` / `.fbx`、生成スクリプト `SourceArt/StageClear/Scripts/build_victory.py`。
  UEからエクスポートしたPlayerスケルトン＋実際の網Mesh（`hand_r_net_socket` の相対Transform, Scale 0.63）で制作。
- VFX：`/Game/REBoarGetch/Art/VFX/StageClear/NS_StageClearStars`（RadialBurstテンプレートから作成、User.Color）、`M_StageClearStar`、`T_StageClearStar`。
  分解は既存 `M_CaptureDataTransfer` / `M_CaptureGlow` / `NS_CaptureSuccess` を演出専用MIDで再利用（元Materialは変更しない）。

## Scripts（再実行可能）
- `Verification/StageClearSetupAssets.py`：星Texture/Material、Victory FBX import、Montage。
- `Verification/StageClearNiagara.py create|configure`：NiagaraToolsets経由で星Niagaraを作成・設定。
- `Verification/StageClearConfigureBP.py`：WBP_Result（Text/UMGアニメ/SE）、PlayerController Component、GameModeを設定。
- `Verification/StageClearPIE.py`：PIEでの実動作検証（SaveGamesを退避・復元）。
