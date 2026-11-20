#pragma once

#include "CoreMinimal.h"
#include "Boar/BoarDataAsset.h"
#include "Components/ActorComponent.h"
#include "FootstepDustComponent.generated.h"

class UNiagaraSystem;
class USkeletalMeshComponent;

/** 1歩の土煙の量・勢いです。MinSpeedのときの値とFullStrengthSpeedのときの値の間を、移動速度で補間します。 */
USTRUCT(BlueprintType)
struct REBOARGETCH_API FFootstepDustStrength
{
	GENERATED_BODY()

	FFootstepDustStrength() = default;
	FFootstepDustStrength(int32 InSpawnCount, float InSizeScale, float InLifetime, float InKickSpeed, float InKickUp, int32 InStreakCount)
		: SpawnCount(InSpawnCount), SizeScale(InSizeScale), Lifetime(InLifetime), KickSpeed(InKickSpeed), KickUp(InKickUp), StreakCount(InStreakCount)
	{
	}

	/** 1歩で出す土塊の数（Niagara User.SpawnCount）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust", meta = (ClampMin = "0"))
	int32 SpawnCount = 4;

	/** 最大の土塊の大きさ（User.SizeScale、1 = 半径12cm）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust", meta = (ClampMin = "0.0"))
	float SizeScale = 0.5f;

	/** 最も長い寿命（User.Lifetime、秒）です。最短はこの55%です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust", meta = (ClampMin = "0.05", Units = "s"))
	float Lifetime = 0.35f;

	/** 後方へ蹴り出す速さ（User.KickSpeed、cm/s）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust", meta = (ClampMin = "0.0"))
	float KickSpeed = 100.0f;

	/** 上へ舞い上がる速さ（User.KickUp、cm/s）です。低いほど地面すれすれに広がります。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust", meta = (ClampMin = "0.0"))
	float KickUp = 40.0f;

	/** 後方へ流れる細いストリークの本数（User.StreakCount）です。0なら出しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust", meta = (ClampMin = "0"))
	int32 StreakCount = 0;
};

/**
 * 走行中の接地に合わせて、足元から小さな土煙を出す演出専用Componentです。
 *
 * 足音と同じAnimNotify_BoarFootstep（接地フレーム）から呼ばれます。Gameplay・AI・移動には一切関与しません。
 * - 地上で、水平速度がMinSpeed以上のときだけ出します（停止・歩き始め・ジャンプ中・落下中は出しません）。
 * - 速度でMinSpeed〜FullStrengthSpeedの強さを補間します（Player：Walk→Run→Dash、Blue Boar：通常移動→逃走）。
 * - 足のBoneの真下に地面があるときだけ、その地面の位置から出します。
 * - Boarに付いている場合は、BoarArchetypesに含まれる種別・未捕獲・ステージ進行中のときだけ出します。
 */
UCLASS(ClassGroup=(Presentation), meta=(BlueprintSpawnableComponent))
class REBOARGETCH_API UFootstepDustComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFootstepDustComponent();

	/** AnimNotify_BoarFootstepから接地ごとに呼ばれます。条件を満たせば土煙を1回出します。 */
	void HandleFootstep(USkeletalMeshComponent* MeshComp, FName FootBone);

	/** 土煙を出した回数です（テスト・デバッグ用）。 */
	int32 GetDustSpawnCount() const { return DustSpawnCount; }

	/** 演出を有効にするかです。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep Dust")
	bool bEnabled = true;

	/** 出す土煙（既定はNS_FootstepDust）。未設定なら出しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust")
	TObjectPtr<UNiagaraSystem> DustSystem;

	/** Boarに付いている場合、土煙を出す種別です（空なら全種別）。既定はBlue Boar（EscapeSpecialist）だけです。Player等では無視します。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust")
	TArray<EBoarArchetype> BoarArchetypes;

	/** この水平速度（cm/s）未満では出しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Speed", meta = (ClampMin = "0.0"))
	float MinSpeed = 250.0f;

	/** この水平速度（cm/s）以上でFullStrengthの強さになります。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Speed", meta = (ClampMin = "1.0"))
	float FullStrengthSpeed = 900.0f;

	/** 速度による強さの補間カーブの指数です（1 = 直線、大きいほど高速域でだけ強くなります）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Speed", meta = (ClampMin = "0.1"))
	float StrengthExponent = 1.0f;

	/** MinSpeedのときの量・勢いです。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Strength")
	FFootstepDustStrength MinStrength;

	/** FullStrengthSpeed以上のときの量・勢いです。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Strength")
	FFootstepDustStrength FullStrength;

	/** ストリークを出し始める水平速度（cm/s）です。これ未満ではStreakCountに関わらず出しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Strength", meta = (ClampMin = "0.0"))
	float StreakMinSpeed = 0.0f;

	/** 横方向への広がり（KickSpeedに対する割合、User.Spread）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Look", meta = (ClampMin = "0.0"))
	float Spread = 0.6f;

	/** 土煙の色（User.DustColor）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Look")
	FLinearColor DustColor = FLinearColor(0.72f, 0.58f, 0.40f, 1.0f);

	/** ストリークの色（User.StreakColor、Alpha = 不透明度）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Look")
	FLinearColor StreakColor = FLinearColor(0.95f, 0.88f, 0.70f, 0.45f);

	/** ストリークの長さ（User.StreakLength、cm）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Look", meta = (ClampMin = "0.0", Units = "cm"))
	float StreakLength = 70.0f;

	/** 発生位置のずれ（移動方向基準、X=前・Y=右・Z=上、cm）です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Placement")
	FVector LocationOffset = FVector::ZeroVector;

	/** 足の真下に地面を探す距離（cm）です。これより遠ければ空中とみなして出しません。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Placement", meta = (ClampMin = "1.0", Units = "cm"))
	float MaxGroundDistance = 40.0f;

	/** 同じ足から連続して出す最短間隔（秒）です。BlendSpaceの重なりで同じ接地が二重に来ても1回にします。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep Dust|Placement", meta = (ClampMin = "0.0", Units = "s"))
	float MinIntervalPerFoot = 0.1f;

private:
	bool IsAllowedForOwner() const;

	TMap<FName, double> LastDustTimeByFoot;
	int32 DustSpawnCount = 0;
};
