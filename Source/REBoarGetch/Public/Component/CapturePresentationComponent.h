#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraShakeBase.h"
#include "Effects/ACFCameraEffectTypes.h"
#include "ACFCameraSequence.h"
#include "CapturePresentationComponent.generated.h"

class ABoarBase;
class ABoarGameMode;
class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class UAudioComponent;

/** 捕獲成功時の小さなCamera Shakeの基底。既定の揺れはコンストラクタで設定し、BP_CaptureSuccessCameraShakeで調整します。 */
UCLASS(Abstract)
class REBOARGETCH_API UCaptureSuccessCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UCaptureSuccessCameraShake(const FObjectInitializer& ObjectInitializer);
};

/** 捕獲演出中のBoar 1体分の見た目の状態です。 */
USTRUCT()
struct FCaptureVisualTarget
{
	GENERATED_BODY()

	/** データ化する元の捕獲個体への弱参照。 */
	UPROPERTY()
	TWeakObjectPtr<ABoarBase> Boar;

	/** データ化演出を表示するメッシュ。 */
	UPROPERTY()
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;

	/** 演出前のOverlay Material（復元用）。 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> OriginalOverlay;

	/** 発光用のOverlay Material。 */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Glow;

	/** データ化中だけ表示する残像Proxyと、そのMaterial。 */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> DataMesh;

	/** データ化の進行を反映する代理メッシュの動的マテリアル。 */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DataMaterial;

	/** 足元の捕獲リング（CaptureVFX）。 */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> VFX;

	/** データ化中に上昇するデータ粒子（DataFragmentVFX）。捕獲リングとは別に保持します。 */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> DataVFX;

	/** 青白いテレポート（TeleportVFX）。データ化の少し前から転送完了まで、実時間で年齢を指定して進めます。 */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> TeleportVFX;

	/** 演出開始前に元メッシュが表示されていたか。 */
	bool bOriginalVisible = true;
	/** 捕獲地点でのデータ化を開始済みか。 */
	bool bDataStarted = false;
	/** 捕獲地点でのテレポート光を開始済みか。 */
	bool bTeleportStarted = false;

	/** RestoreTarget済み。復元は一度だけ行い、後から別処理が変えた見た目を戻さない。 */
	bool bRestored = false;

	/** 命中時の位置と、DataMaterialへ渡すBoarの下端・高さ（cm）。 */
	FVector CaptureLocation = FVector::ZeroVector;
	/** 捕獲時のメッシュのワールド下端Z（cm）。 */
	float CaptureMinZ = 0.f;
	/** データ化の進行範囲に使うメッシュの高さ（cm）。 */
	float CaptureHeight = 150.f;

	/** 命中からの経過時間（実時間・秒）。 */
	float Age = 0.f;
};

/** 捕獲地点の周囲で一時的に減速しているBoar。値はこのComponentが設定したものだけを元へ戻します。 */
USTRUCT()
struct FCaptureSlowedBoar
{
	GENERATED_BODY()

	/** 捕獲演出で追跡するBoarへの弱参照。 */
	UPROPERTY()
	TWeakObjectPtr<ABoarBase> Boar;

	/** 減速前のMeshのGlobalAnimRateScaleと、このComponentが設定した値。 */
	float OriginalAnimRate = 1.f;
	/** 演出中にBoarへ適用したアニメーション速度倍率。 */
	float AppliedAnimRate = 1.f;

	/** アニメーション倍率・移動時間倍率をこのComponentが設定中か。 */
	bool bAnimApplied = false;
	/** 捕獲個体の移動時間倍率を演出用に変更済みか。 */
	bool bMoveApplied = false;

	/** 他の処理がアニメーション倍率を変えたため、この演出では以後触らない。 */
	bool bAnimOwnershipLost = false;
};

/** 捕獲確定（対象の後片付け）の後も、余韻の光粒子だけを短く残すテレポートVFX。 */
USTRUCT()
struct FCaptureLingeringVFX
{
	GENERATED_BODY()

	/** 捕獲確定後も余韻を残すテレポートVFX。 */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> VFX;

	/** VFXの年齢（テレポート開始からの実時間）と、破棄する年齢。 */
	float Age = 0.f;
	/** 余韻VFXを破棄する年齢（テレポート開始からの実時間の秒）。 */
	float EndAge = 0.f;
};

// Boarが非表示・破棄されても演出の終わりまで位置を保つ軽量な演出用Actor。Gameplayには一切関与しない（Collision・Tick・Replicationなし）。CapturePresentationComponentだけが生成・破棄する。
/** 捕獲Orbitカメラの注視点。 */
UCLASS(NotBlueprintable, Transient)
class REBOARGETCH_API ACaptureFocusProxy : public AActor
{
	GENERATED_BODY()

public:
	ACaptureFocusProxy();
};

/** Cageへ収容した直後の受信エフェクト1つ分。 */
USTRUCT()
struct FCaptureReception
{
	GENERATED_BODY()

	/** 檻の受信位置で再生中のNiagaraコンポーネント。 */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> VFX;

	/** 受信VFX開始後の実時間（秒）。 */
	float Age = 0.f;
};

// 捕獲成功時の見た目・音・カメラを制御します。捕獲判定とCageへの収容は既存のGameplay処理が担当します。
// 捕獲成功演出の調整用Component。捕獲地点の演出時間はHit Stop、Slow、Dissolve、Transfer Finishの合計です。Cage受信演出はその後に再生します。
/** 捕獲成功時の見た目・音・カメラを制御し、完了時に進行側へ通知する。 */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UCapturePresentationComponent : public UActorComponent
{
	friend class FBoarStageEndRegressionTest;

	GENERATED_BODY()

public:
	UCapturePresentationComponent();

	/** 捕獲されたBoarの演出を開始します。bSharedFeedbackはNet 1回の最初の捕獲だけtrue（Hit Stop・カメラ・音を1組だけ出す）。 */
	bool PresentCapturedBoar(ABoarBase* Boar, bool bSharedFeedback);

	/** 演出を打ち切ります。bCommitCaptures時は演出待ちの捕獲をGameModeへ確定させます（Stage終了後はGameMode側で無視されます）。 */
	void CancelPresentation(bool bCommitCaptures = true);

	bool HasPendingCaptures() const { return bCommittingCaptures || !Targets.IsEmpty() || !Receptions.IsEmpty(); }

	/** 捕獲成功演出を有効にします。OFFでも捕獲判定とCageへの収容は通常どおり行われます。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation")
	bool bEnabled = true;

	// ---- タイミング・局所減速 ----
	// Hit Stop／Slowは、捕獲地点の周囲にいる未捕獲Boarだけを減速します。World全体の時間（Global Time Dilation）、
	// Player、AI（StateTree）、Timer、Stage進行、Cage、UI・SE・VFXの時間は変えません。

	/** 網が当たった直後に周囲のBoarを止めて見せる長さ（実時間・秒）。推奨0.08～0.12秒。Net 1回につき1回だけ適用します。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0", ClampMax="0.12"))
	float HitStopDuration = .1f;

	/** Hit Stop後に周囲のBoarをゆっくり動かす長さ（実時間・秒）。足元リングとシアン発光を見せる時間です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0", ClampMax="0.6"))
	float SlowDuration = .27f;

	/** 減速させるBoarの範囲（捕獲地点からの半径）。捕獲されたBoar自身とPlayerは対象外です。0なら周囲のBoarを減速しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ClampMin="0", ClampMax="5000", Units="cm"))
	float LocalSlowRadius = 1500.f;

	/** Hit Stop中の周囲Boarのアニメーション再生倍率（MeshのGlobalAnimRateScale）。0に近いほど止まって見えます。初期値0.01。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ClampMin="0.001", ClampMax="1"))
	float HitStopAnimRate = .01f;

	/** Slow中の周囲Boarのアニメーション再生倍率（MeshのGlobalAnimRateScale）。初期値0.3。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ClampMin="0.01", ClampMax="1"))
	float SlowAnimRate = .3f;

	/** ONなら移動の時間倍率にアニメーション再生倍率と同じ値を使います。ABPは実際の速度から足の再生速度を合わせるため、同じ値にすると足と移動がずれません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow")
	bool bMatchMovementToAnimation = true;

	/** Hit Stop中の周囲Boarの移動時間倍率（移動・加速・旋回）。MaxWalkSpeedは変えません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ClampMin="0.001", ClampMax="1", EditCondition="!bMatchMovementToAnimation"))
	float HitStopMoveScale = .01f;

	/** Slow中の周囲Boarの移動時間倍率（移動・加速・旋回）。MaxWalkSpeedは変えません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Local Slow", meta=(ClampMin="0.01", ClampMax="1", EditCondition="!bMatchMovementToAnimation"))
	float SlowMoveScale = .3f;

	/** Boarが下から上へデータ化される長さ（実時間・秒）。長くするとScanと分解をゆっくり見せられます。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05", ClampMax="0.7"))
	float CaptureDissolveDuration = .3f;

	/** データ化後、Boarの残像と破片が上昇して消えるまでの長さ（実時間・秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05", ClampMax="0.4"))
	float TransferFinishDuration = .18f;

	/** Cageへ収容した直後のシアン受信エフェクトの長さ（実時間・秒）。捕獲地点の演出時間には含まれません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Timing", meta=(ClampMin="0.05", ClampMax="0.5"))
	float CageReceptionDuration = .18f;

	// ---- カメラ（ACFが無いときのFOV Punch In） ----

	/** 捕獲時に一時的に狭める視野角（度）。大きいほどPunch Inが強くなります。位置やCamera Actorは切り替えません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ClampMin="0", ClampMax="20"))
	float CameraZoomAmount = 12.f;

	/** Punch Inして元の視野角へ戻るまでの時間（実時間・秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera", meta=(ClampMin="0.01", ClampMax="1"))
	float CameraDuration = .38f;

	// ---- VFX・Material ----

	/** 転送中だけ表示するBoar用Material。下から上へのDissolve、シアンのScan境界、データ線を描きます。元のBoar Materialは変更しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX")
	TObjectPtr<UMaterialInterface> DataMaterial;

	/** 転送前のBoarのシアン発光に使うMaterial。未設定だと発光が表示されません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX")
	TObjectPtr<UMaterialInterface> GlowMaterial;

	/** 捕獲地点のBoar足元に出すシアンリングのNiagara System。拡大してから薄くなります。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX")
	TObjectPtr<UNiagaraSystem> CaptureVFX;

	/** Cageへの収容直後に出す受信エフェクト。未設定の場合は受信エフェクトを表示しません（Errorを出します）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX")
	TObjectPtr<UNiagaraSystem> CageReceptionVFX;

	/** 捕獲リングとCage受信エフェクトを、BoarのActor位置からどれだけ下げて出すか（cm）。足元に合わせるための値です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0", Units="cm"))
	float CaptureRingHeightOffset = 35.f;

	// ---- 色 ----

	/** 捕獲リング・データ化Material・データ片に使う基本色です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Color")
	FLinearColor CaptureColor = FLinearColor(.01f, .75f, 1.f);

	/** 命中直後のBoar発光の開始色です。データ化開始までにGlow Peak Colorへ変化します。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Color")
	FLinearColor GlowStartColor = FLinearColor(.01f, .52f, .9f);

	/** データ化直前のBoar発光の色です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Color")
	FLinearColor GlowPeakColor = FLinearColor(.04f, .88f, 1.f);

	/** Cageへ収容した直後の受信エフェクトの色です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Color")
	FLinearColor ReceptionColor = FLinearColor(.02f, .9f, 1.f);

	/** 捕獲リングとCage受信エフェクトの基準Scale。大きくすると両方のリングが広がります。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0.01"))
	float VFXScale = .55f;

	/** データ化開始時に捕獲地点から上昇するデータ粒子（四角い破片と縦長の光片）のNiagara System。粒子数・サイズ・速度・遅延・消失はこのSystemで調整します。未設定なら粒子を表示しません（捕獲処理には影響しません）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX")
	TObjectPtr<UNiagaraSystem> DataFragmentVFX;

	/** 転送完了時にBoarのデータ残像が上へ移動する距離（cm）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0", ClampMax="250"))
	float TransferRiseDistance = 105.f;

	// ---- 効果音（命中・転送・確定・複数体の4音は「Audio V2」カテゴリ） ----

	/** 捕獲Orbitカメラ（カメラがBoarの周りを回る間）の効果音。Orbitを再生したときだけ開始と同時に鳴らし、Spiral OrbitのDurationの終わりに合わせてフェードアウトします。Orbitを省略したとき・ACFが無いときは鳴らしません。未設定なら無音です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Audio")
	TObjectPtr<USoundBase> OrbitSound;

	/** Orbit Soundの音量倍率（共通のSE音量に掛けます）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Audio", meta=(ClampMin="0", ClampMax="2"))
	float OrbitSoundVolume = 1.f;

	/** Orbitの終わりに向けてOrbit Soundを消すフェードアウトの長さ（秒）。Orbitの終わりちょうどで無音になります。Orbitが中断されたときはすぐに止めます。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Audio", meta=(ClampMin="0", ClampMax="2", Units=s))
	float OrbitSoundFadeOutTime = .25f;

	/** 網の命中時に再生する小さなCamera Shake。Net 1回につき1回だけ再生します。振幅・周波数・Blend時間はBP_CaptureSuccessCameraShakeのClass Defaults（Root Shake Pattern）で調整します。未設定なら揺れません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera")
	TSubclassOf<UCameraShakeBase> CameraShake;

	// ---- 命中インパクト。捕獲の確定とは別に、命中した瞬間だけを短く強調します。 ----

	/** 網が命中した瞬間にBoarの体の位置で1回だけ出す短いNiagara（星形の弾け）。捕獲したBoarごとに出ます。粒子数・大きさ・寿命はこのSystemで調整します。未設定なら出しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact")
	TObjectPtr<UNiagaraSystem> ImpactVFX;

	/** 命中インパクトの色（User.Colorへ渡します）。成功の瞬間なので白～黄色の明るい色を推奨します。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact")
	FLinearColor ImpactColor = FLinearColor(1.f, .92f, .45f);

	/** 命中インパクトのScale。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ClampMin="0.01", ClampMax="5"))
	float ImpactVFXScale = .6f;

	/** 命中インパクトを出す高さ（Boarの足元=0、頭頂=1）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ClampMin="0", ClampMax="1"))
	float ImpactHeightRatio = .6f;

	/** 命中直後にBoarを白く光らせる長さ（実時間・秒）。その後はGlow Start Colorからの通常の発光へ移ります。0で無効。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ClampMin="0", ClampMax="0.3", Units="s"))
	float HitFlashDuration = .08f;

	/** 命中直後のフラッシュの色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact")
	FLinearColor HitFlashColor = FLinearColor(1.f, 1.f, 1.f);

	/** 命中直後のフラッシュの強さ（Glow MaterialのGlowAmount）。通常の発光の最大は1です。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Impact", meta=(ClampMin="0", ClampMax="5"))
	float HitFlashGlowAmount = 2.2f;

	// ---- 転送（吸い込み） ----

	/** 転送完了時のデータ残像の横方向の大きさ（元=1）。上昇しながら細く吸い込まれて消えます。1で縮めません。元のBoarのMeshは変更しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|VFX", meta=(ClampMin="0.05", ClampMax="1"))
	float TransferEndWidthScale = .35f;

	// ---- ACFカメラ演出。ACFの通常カメラが出ている間だけ使い、出ていなければ従来のFOV Punch In（FollowCamera）へ戻します。 ----

	/** ACFカメラ使用中に、Net 1回につき1回再生するCamera Effect Preset（FOV Punch・捕獲地点から押し返す方向性のあるImpulse）。強さ・時間はPresetのLayerで調整します。ACFのRequestが無いときと未設定のときは、Camera Zoom AmountのFOV Punch Inを使います。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera")
	TObjectPtr<class UACFCameraEffectPreset> CameraEffect;

	/** 1回のNetでMulti Capture Threshold体以上を捕獲したときだけ、通常のCamera Effectと入れ替えて再生する強めのPreset。未設定なら通常のPresetのままです。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Camera")
	TObjectPtr<class UACFCameraEffectPreset> MultiCaptureCameraEffect;

	// ---- 捕獲Focusカメラ。ACFのCamera Sequenceで、命中した瞬間だけ短くBoarと網へ寄せてから通常のTPSカメラへ戻します。 ----

	/** ONなら、ACFの通常カメラが出ているときにNet 1回につき1回、捕獲Focusカメラ（Capture Focus Sequence）を再生します。ACFが無効・未要求のときは再生せず、従来のFOV Punch Inを使います。Playerの操作・入力は止めません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Focus Camera")
	bool bUseFocusCamera = true;

	/** 捕獲カメラ演出（DA_CaptureCameraPresentation）。回転（Boarを中心に約360°公転しながら上昇）・揺れ（Camera Effect Layer）・壁際のCollision・時間とBlendを1つのDataAssetで調整します。設定されていればFocus Sequenceより優先し、捕獲のたびにこの内容からACFのSequenceを組み立てて再生します（PIE中の変更も次の捕獲から反映）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Focus Camera")
	TObjectPtr<class UCaptureCameraPresentationData> CameraPresentation;

	/** 捕獲Orbitカメラ（CS_CaptureOrbit）。最初のShotのPreset（DA_CaptureOrbitShot）のSpiral Orbitで、捕獲したBoar（Focus Proxy）を中心にカメラが約360°公転しながら上昇し、ShotのDuration後にSequenceのReturn Blendで通常のTPSへ戻ります。回転角・半径・高さ・時間・Ease CurveはそのPresetで調整します。Net 1回につき1つだけ再生し、軌道が壁で塞がりすぎる場所では再生しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Focus Camera")
	TObjectPtr<class UACFCameraSequence> FocusSequence;

	/** （現在は使用しません）Orbitは1回のNetにつき1つで、途中で入れ替えると視点が跳ぶため、複数体はBoar群の中心へ注視点を移すだけにしています（Focus On Group Center）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Focus Camera")
	TObjectPtr<class UACFCameraSequence> MultiCaptureFocusSequence;

	// ---- 青白いテレポート。データ化（Dissolve）に合わせてBoarを光で包み、上方へ収束させて消します。 ----

	/** 転送の青白いテレポートNiagara（NS_CaptureTeleport）。Boarの足元に出し、Teleport Lead Time前からCapture Dissolve Duration＋Transfer Finish Durationの終わりまで実時間で進めます。長さはUser.TransferDurationとして渡すので、Niagara内の発生時間はその割合で決まります。粒子数・大きさ・速さ・寿命はSystemのUser Parameterで調整します。未設定なら出しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Teleport")
	TObjectPtr<UNiagaraSystem> TeleportVFX;

	/** データ化（Dissolve）が始まる何秒前からテレポート光を出すか。Boarが螺旋の光に包まれてから消え始めます。捕獲確定・Cage収容の時刻は変わりません。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Teleport", meta=(ClampMin="0", ClampMax="0.6", Units="s"))
	float TeleportLeadTime = .3f;

	/** 捕獲確定（Boar消失）の後、光柱が弱まり光粒子が上空へ抜けて消えるまで残す長さ（User.TailDuration）。Gameplayの時刻には影響しません。Clear・GameOver・Retry・中断では即座に消します。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Teleport", meta=(ClampMin="0", ClampMax="2", Units="s"))
	float TeleportTailDuration = .85f;

	// ---- Orbitカメラの注視点と安全確認 ----

	/** 注視点の高さ（Boarの足元=0、頭頂=1）。命中時のBoundsから計算し、Focus Proxyに保持します（Boarが消えても注視点は動きません）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Focus Camera", meta=(ClampMin="0", ClampMax="1"))
	float FocusHeightRatio = .5f;

	/** ONなら、同じNetで複数体を捕獲したとき、注視点を捕獲したBoar群の中心へ移します（Orbitは1つのまま。Spiral OrbitのFocus Damping Timeで滑らかに移動）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Focus Camera")
	bool bFocusOnGroupCenter = true;

	/** Orbit開始前に、螺旋の軌道上を何点調べて壁・地形を確認するか。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Focus Camera", meta=(ClampMin="4", ClampMax="64"))
	int32 OrbitProbeSamples = 16;

	/** 調べた点のうち、この割合を超えて塞がっていたらOrbitを省略し、通常カメラ（＋Camera Effect）のままにします。塞がりが少ない場合はOrbitを再生し、ACFのCollisionが半径を縮めて壁の内側へ入らないようにします。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Focus Camera", meta=(ClampMin="0", ClampMax="1"))
	float OrbitMaxBlockedFraction = .4f;

	/** 注視点からカメラ位置までの距離のうち、この割合より手前で何かに当たる点を「塞がっている」とみなします。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Focus Camera", meta=(ClampMin="0.1", ClampMax="1"))
	float OrbitMinClearanceRatio = .55f;

	/** テレポート光の外周色（User.Color）。シアン～淡い青。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Teleport")
	FLinearColor TeleportColor = FLinearColor(.12f, .62f, 1.f);

	/** テレポート光の中心色（User.CoreColor）。白に近い青。強くしすぎると白飛びします。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Teleport")
	FLinearColor TeleportCoreColor = FLinearColor(.72f, .9f, 1.f);

	/** テレポート光の大きさの倍率。Boarの大きさ（User.Radius・User.Height）にこの倍率を掛けて渡します。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Teleport", meta=(ClampMin="0.1", ClampMax="5"))
	float TeleportVFXScale = 1.f;

	// ---- 複数体捕獲 ----

	/** 1回のNetで何体以上捕獲したら複数体の強調（確定音をCapture Multi V2にする・Multi Capture Camera Effect）を出すか。強調はNet 1回につき1回だけです。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture Presentation|Multi Capture", meta=(ClampMin="2", ClampMax="10"))
	int32 MultiCaptureThreshold = 2;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void OnWorldTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	// ---- OnWorldTickの各段階（この順で実行します） ----
	void UpdateSharedFeedback(float RealDelta);
	void UpdateTargets(float RealDelta, TArray<TWeakObjectPtr<ABoarBase>>& OutFinished);
	bool UpdateTarget(FCaptureVisualTarget& Target, float RealDelta);
	void CommitFinishedCaptures(ABoarGameMode* Mode, const TArray<TWeakObjectPtr<ABoarBase>>& Finished);
	void UpdateReceptions(float RealDelta, ABoarGameMode* Mode);

	// ---- 共有状態（周囲Boarの局所減速・Camera FOV・Camera Shake）の取得と解放 ----
	// 解放は冪等で、このComponentが最後に設定した値のまま残っている場合だけ元の値へ戻します（他の演出が上書きした値は戻しません）。
	void CollectLocalSlowTargets(const FVector& Center);
	void ApplyLocalSlow(bool bHitStop);
	void ReleaseLocalSlow();
	void RestoreSlowedBoar(FCaptureSlowedBoar& Slowed);
	void ApplyFieldOfView(float FOV);
	void ReleaseCamera();
	void ReleaseCameraShake();

	/**
	 * 共有演出（局所減速・FOV・Camera Shake・Camera Effect）を元へ戻します。Focusカメラはここでは止めません。
	 * bForPauseは現在どの処理にも使われていません（Pause時もFocusカメラはACFと一緒に止まるため）。
	 */
	void RestoreSharedState(bool bForPause = false);

	void ReportMissingVisualAssets() const;

	/** bKeepTeleportTail=trueなら（正常確定・Boar消滅）テレポートVFXを余韻用にLingeringへ引き継ぐ。Cancelではfalse（即座に破棄）。 */
	void RestoreTarget(FCaptureVisualTarget& Target, bool bKeepTeleportTail = false);

	void StartDataConversion(FCaptureVisualTarget& Target);
	void StartCageReception(const FVector& Location);

	/** 捕獲地点の演出時間（Hit Stop＋Slow＋Dissolve＋Transfer Finish）。この時点で捕獲が確定します。 */
	float GetTotalDuration() const;

	// ---- 効果音 ----
	void BeginCaptureAudio(ABoarBase* Boar);
	void UpdateCaptureAudio(float RealDelta);
	void ConfirmCaptureAudio(const TArray<ABoarBase*>& Confirmed);
	void StopCaptureAudio(bool bKeepConfirmedTail = false);
	UAudioComponent* SpawnCaptureAudio(USoundBase* Sound, float Volume, float Pitch = 1.f);

	// ---- 捕獲音（Audio V2）。未設定の音は鳴らしません。Net 1回につき1組だけ鳴らします。 ----

	/** 命中した瞬間の音。 */
	UPROPERTY(EditAnywhere, Category="Capture Presentation|Audio V2")
	TObjectPtr<USoundBase> CaptureImpactV2;

	/** 命中からデータ化の終わりまで、ピッチ・音量・Low Passを上げながら鳴らし続ける転送音。 */
	UPROPERTY(EditAnywhere, Category="Capture Presentation|Audio V2")
	TObjectPtr<USoundBase> CaptureSpiralV2;

	/** 捕獲確定（Cage収容）の音。 */
	UPROPERTY(EditAnywhere, Category="Capture Presentation|Audio V2")
	TObjectPtr<USoundBase> CaptureConfirmV2;

	/** 複数体捕獲のときに確定音の代わりに鳴らす音。 */
	UPROPERTY(EditAnywhere, Category="Capture Presentation|Audio V2")
	TObjectPtr<USoundBase> CaptureMultiV2;

	/** 捕獲命中SEを停止・調整するための再生参照。 */
	TWeakObjectPtr<UAudioComponent> CaptureImpactVoice;
	/** 捕獲地点の螺旋SEを停止・調整するための再生参照。 */
	TWeakObjectPtr<UAudioComponent> CaptureSpiralVoice;
	/** 捕獲確定SEを停止・調整するための再生参照。 */
	TWeakObjectPtr<UAudioComponent> CaptureConfirmVoice;

	/** 捕獲音を鳴らしているNetで捕獲したBoar。確定通知にこのBoarが含まれていたら確定音を鳴らします。 */
	TArray<TWeakObjectPtr<ABoarBase>> CaptureAudioTargets;
	/** 捕獲音の進行を測る実時間（秒）。 */
	float CaptureAudioAge = 0.f;

	// ---- カメラ ----
	void SpawnImpact(const FCaptureVisualTarget& Target);

	/** ACFが通常カメラを出していればCamera Effectを再生してtrue。出ていなければfalse（従来のFOV Punch Inを使う）。 */
	bool PlayCameraEffect(class UACFCameraEffectPreset* Preset, const FVector& Source, bool bReplaceImmediately);
	void ReleaseCameraEffect(bool bImmediate);
	void PlayMultiCaptureFeedback(const FVector& Source, ABoarBase* Boar);

	/** Focusカメラを再生する。再生中の自分のFocusは新しい再生の開始後に止める（Requestを二重に持たない）。 */
	bool PlayFocusCamera(class UACFCameraSequence* Sequence, AActor* Target);

	/** 今回再生するFocus Sequence。Camera Presentationがあればそこから組み立て、無ければFocus Sequence。 */
	class UACFCameraSequence* ResolveFocusSequence();

	/** 自分が再生したFocusだけを終え、Focus Proxyを破棄する。bCancel=trueならCancel Blend（カット）、falseならReturn Blendで戻す。何度呼んでも安全。 */
	void ReleaseFocusCamera(bool bCancel);

	/** 命中したBoarのBoundsから注視点を決め、Focus Proxyを作る（既にあれば移動のみ）。 */
	AActor* PlaceFocusProxy(const FVector& FocusPoint);
	FVector ComputeFocusPoint(const ABoarBase* Boar) const;

	/** Sequenceの最初のShotにあるSpiral Orbit（無ければnull）。 */
	const class UACFCameraNode_SpiralOrbit* FindSpiralOrbit(const class UACFCameraSequence* Sequence) const;

	/** Orbitの開始角（ACFと同じ規則：Control RotationのYaw+180+Start Angle Offset）。 */
	float ComputeOrbitStartYaw(const class UACFCameraNode_SpiralOrbit& Orbit, const FVector& Focus) const;

	/** 螺旋の軌道上をSweepし、塞がっている割合がOrbit Max Blocked Fraction以下ならtrue。 */
	bool IsOrbitFeasible(const class UACFCameraSequence* Sequence, const FVector& Focus, const ABoarBase* Boar) const;

	/** Focusカメラの自然終了の検出とデバッグ表示（Boar.Capture.OrbitDebug）。 */
	void UpdateFocusCamera(float RealDelta);

	// ---- テレポートの余韻 ----
	void UpdateLingering(float RealDelta);
	void DestroyLingering();
	void StartTeleport(FCaptureVisualTarget& Target);

	/** 命中からテレポート光を出し始めるまでの時間（データ化開始のTeleport Lead Time前）。 */
	float GetTeleportStart() const;

	class UACFCameraSubsystem* GetACFSubsystem() const;

	UFUNCTION()
	void OnTargetDestroyed(AActor* Actor);

	// ---- 実行中の状態 ----

	/** 同じ演出で処理している捕獲個体の状態一覧。 */
	UPROPERTY(Transient)
	TArray<FCaptureVisualTarget> Targets;

	/** 檻で再生中の受信VFX一覧。 */
	UPROPERTY(Transient)
	TArray<FCaptureReception> Receptions;

	/** FOV Punch Inを掛けているFollowCamera（ACFを使っていないときだけ）。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCameraComponent> Camera;

	/** 演出終了時に停止するカメラシェイク。 */
	UPROPERTY(Transient)
	TObjectPtr<UCameraShakeBase> ActiveShake;

	/** 現在の共有演出で減速対象にしたBoar。演出終了時に空にします。 */
	UPROPERTY(Transient)
	TArray<FCaptureSlowedBoar> SlowedBoars;

	/** 実時間の演出更新を解除するためのハンドル。 */
	FDelegateHandle TickHandle;
	/** 実時間Deltaを計算する直前の時刻（秒）。 */
	double LastRealTime = 0.;

	/** 共有演出の経過時間（実時間・秒）と、Punch In前のFOV。 */
	float SharedAge = 0.f;
	/** Punch In解除時に復元するカメラのFOV（度）。 */
	float OriginalFOV = 90.f;

	/** このComponentが最後に設定したFOV。解放時の所有確認に使います。 */
	float AppliedFOV = 90.f;

	/** GameModeへ捕獲確定を通知している最中か（HasPendingCaptures用）。 */
	bool bCommittingCaptures = false;

	/** 複数個体で共有する捕獲演出が進行中か。 */
	bool bSharedActive = false;
	/** このコンポーネントが現在の演出カメラを所有しているか。 */
	bool bOwnCamera = false;

	/** 再生中のACF Camera Effect。共有演出の終了・キャンセル・Pause・EndPlayで止めます。 */
	FACFCameraEffectHandle ActiveCameraEffect;

	/** 共有演出のカメラをACF Effectで出しているか（trueならFollowCameraのFOVには触れない）。 */
	bool bUsingACFEffect = false;

	/** 再生中の捕獲Focusカメラ（ACF Camera Sequence）。Net 1回につき最大1つ。 */
	FACFCameraSequenceHandle ActiveFocus;

	/** 再生中（または最後に再生した）Focus Sequence。Camera Presentationから組み立てた一時オブジェクトもここで保持する。 */
	UPROPERTY(Transient)
	TObjectPtr<class UACFCameraSequence> ActiveFocusSequence;

	/** Orbitの注視点（演出用Proxy）。Focusカメラの終了・Cancel・EndPlayで破棄する。 */
	UPROPERTY(Transient)
	TObjectPtr<AActor> FocusProxy;

	/** 現在のNet使用で捕獲したBoarの注視点（群の中心を求める）。 */
	TArray<FVector> FocusPoints;

	// ---- デバッグ：Orbit開始時の注視点・開始角・経過、実カメラが注視点の周りを回った累積角と高さ ----
	/** Orbit開始時点で固定した注視位置（ワールド座標）。 */
	FVector OrbitFocusAtStart = FVector::ZeroVector;
	/** Orbit開始時のワールドYaw（度）。 */
	float OrbitStartYaw = 0.f;
	/** Orbit開始からの実時間（秒）。 */
	float OrbitRealAge = 0.f;
	/** Orbitで実際に回った角度の累積（度）。 */
	float OrbitActualDegrees = 0.f;
	/** Orbit回転差分の測定に使う前回のYaw（度）。 */
	float OrbitPrevYaw = 0.f;
	/** Orbit中に記録した注視点からの最低カメラ高（cm）。 */
	float OrbitMinHeight = 0.f;
	/** Orbit中に記録した注視点からの最高カメラ高（cm）。 */
	float OrbitMaxHeight = 0.f;
	/** Orbit回転差分の基準となるYawを取得済みか。 */
	bool bOrbitHasPrevYaw = false;
	/** Orbit中に記録したカメラのワールド位置列（デバッグ用）。 */
	TArray<FVector> OrbitTrail;

	/** Orbit Soundの再生。Orbitの開始で鳴らし、Orbitの終わり・中断で止める（捕獲音とは別の寿命）。 */
	void StartOrbitSound(const class UACFCameraNode_SpiralOrbit* Orbit);
	void UpdateOrbitSound();
	void StopOrbitSound(bool bFade);
	/** OrbitループSEの音量を制御する再生参照。 */
	TWeakObjectPtr<UAudioComponent> CaptureOrbitVoice;
	/** OrbitループSEの減衰を始める時刻（Orbit開始からの実時間の秒）。 */
	float OrbitSoundFadeStart = 0.f;
	/** OrbitループSEを減衰中か。 */
	bool bOrbitSoundFading = false;

	/** 捕獲確定後に余韻を残すテレポートVFX。 */
	UPROPERTY(Transient)
	TArray<FCaptureLingeringVFX> Lingering;

	/** 現在のNet使用で捕獲した数と、複数体の強調を出したか。 */
	int32 SameUseCaptureCount = 0;
	/** 複数捕獲の追加フィードバックを再生済みか。 */
	bool bMultiFeedbackPlayed = false;
};
