# GameOver presentation（ダウン演出 / Linear AKI-75）

方針：レトロアクション風のダウン演出＋サイバーシステム停止風の警告UI。明るさは残し、ダウンを隠す長いグリッチは使わない。

## 責務（C++はロジックと進行、BPは演出）
| 層 | クラス / Asset | 担当 |
| --- | --- | --- |
| 進行（C++） | `ABoarGameMode` | HP0→GameOver確定、同フレームClear優先、入力・AI・Damage・Pause停止、完了後のUI表示可否 |
| 進行制御（C++） | `UGameOverPresentationComponent`（`ABoarPlayerController::GameOverPresentation`） | 演出BPの生成・開始（1回だけ）、`SafetyTimeout`（実時間6s）、中断、完了報告→`CompleteStageEndPresentation()`。演出BP未設定なら即UI |
| 演出部品（C++） | `AGameOverPresentationDirector` | 毎フレーム処理が要る下位処理：実時間ヒットストップ、SpringArm後退、Montage保持、跳ね返り、発光減衰、効果Widget値、終了時の復元 |
| 演出内容（BP） | `/Game/BP/Presentation/BP_GameOverPresentation` | 順序・タイミング（Delay）・SE・各部品の引数、Class DefaultsでMontage・カメラ値・発光Material |
| 表示（C++描画） | `UBoarGameOverEffectWidget`, `UBoarGameOverWidget` | 低レベルSlate描画（警告線・ノイズ・枠）。色・線・文言・時間はClass Defaultsで調整 |

## Flow
Boar接触（Capsule Overlap または Blocking Hit）→ HP0 → `EnterDowned()`（Stunなし）→ `RequestGameOver` → フレーム終端 `ResolveStageEnd`
→ `OnGameOver()`（BP） → `GameOverPresentation->StartPresentation()` → `BP_GameOverPresentation.OnPresentationStarted`
→ BPが `FinishPresentation` → Component → `CompleteStageEndPresentation()` → `HandleStageGameOver()` → `WBP_GameOver` → `OnGameOverUIShown`（BP）。

BP（`OnPresentationStarted`）のタイムライン（実時間。演出BPのDelayはGlobal Time Dilationを打ち消して進みます）：
| 時刻 | ノード |
| --- | --- |
| 0.00 | BeginRedFlash(0.5, 0.24) / Impact SE / StopStageBGM(0.8) / BeginHitStop(0.09, 0.05) / SettlePlayerOnGround / KnockBackNearbyBoars(260, 170, 35, 0.4) |
| 0.02 | PlayNoiseBurst(0.18, 0.8) + Noise SE |
| 0.10 | PlayDownAnimation（`AM_Player_GameOver_Down`） |
| 0.15 | BeginGadgetGlowDecay(1.1) / FadeOutHUD(0.4) |
| 0.25 | BeginWarning(0.35) |
| 0.45 | PowerDown SE |
| 0.80 | BeginCameraPullBack(1.4, 0.6) |
| 1.14 | BodyFall SE / PlayNoiseBurst(0.18, 0.56) + Noise SE |
| 2.50 | FinishPresentation |
`OnGameOverUIShown`：Jingle SE / SetWarningAlphaForUI(0.35)。

中断・EndPlay（Retry/Lobby）では演出BPが時間倍率・SpringArm・Probe Channel・FOV・Overlay・HUD不透明度・効果Widgetを戻します。

## Editor tuning
- タイミング・SE：`BP_GameOverPresentation` のEvent Graph（Delayの秒数、各ノードのピン）。
- アニメ・カメラ・発光：`BP_GameOverPresentation` のClass Defaults（Animation / Camera / Warning）。
- 有効/無効・上限時間・使用する演出BP：`BP_BoarPlayerController > GameOverPresentation`（`bEnabled`, `DirectorClass`, `SafetyTimeout`）、`BP_BoarGameMode > bUseGameOverPresentation`。
- `WBP_GameOver`（Class Defaults > Reveal）：見出しの拡大・枠。見出しの文字・色はDesignerの `Heading`。

## Assets
- Animation：`/Game/Animation/Player/A_Player_GameOver_Down`（1.4s, Root Motionなし）、`AM_Player_GameOver_Down`（FullBody, Auto Blend Out無効）。
- Audio：`/Game/REBoarGetch/Art/Audio/GameOver/SFX_GameOver_{Impact,Noise,PowerDown,BodyFall,Jingle}`（`SourceArt/GameOver/Scripts/gen_sfx.py`）。
- Blender：`SourceArt/GameOver/`（git管理外。`Scripts/build_down.py`）。

## Scripts（再実行可能）
- `Verification/GameOverSetupAssets.py`：Anim/Montage/SE Import、WBP見出し。
- `Verification/PresentationBPSetup.py`：演出BP（Stage Clear / GameOver）のClass Defaults・Event Graph作成、Controller接続、保存後の再取得。
- `Verification/GameOverPIE.py`（GUI、明示依頼時のみ）：実接触でHP0→演出→UI、Retry、檻破壊、Lobby、同フレームClear優先。
