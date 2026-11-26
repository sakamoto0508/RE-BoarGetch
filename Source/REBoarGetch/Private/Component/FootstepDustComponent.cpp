#include "Component/FootstepDustComponent.h"

#include "Boar/BoarBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

namespace FootstepDust
{
	/** 地面を探すTraceの開始位置を足のBoneより少し上（cm）にして、Boneが地面へ少しめり込んでいても当たるようにする。 */
	constexpr float GroundTraceStartOffset = 15.0f;
}

UFootstepDustComponent::UFootstepDustComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// 既定のアセット。各BPのComponent（Details）で差し替え・解除できる。
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> DustFinder(TEXT("/Game/REBoarGetch/Art/VFX/Common/NS_FootstepDust"));
	DustSystem = DustFinder.Object;
}

bool UFootstepDustComponent::IsAllowedForOwner() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!bEnabled || !DustSystem || !Character)
		return false;

	if (const ABoarBase* Boar = Cast<ABoarBase>(Character))
	{
		if (Boar->IsCaptured())
			return false;

		if (BoarArchetypes.Num() > 0 && !BoarArchetypes.Contains(Boar->GetBoarArchetype()))
			return false;
	}

	// Capture演出・クリア・GameOver・中断中は出さない（GameModeが無いテスト用Levelでは出す）。
	return ABoarGameMode::IsStageProgressAllowed(this);
}

void UFootstepDustComponent::HandleFootstep(USkeletalMeshComponent* MeshComp, FName FootBone)
{
	UWorld* World = GetWorld();
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!MeshComp || !World || !World->IsGameWorld() || !Character || !IsAllowedForOwner())
		return;

	// 地上で実際に走っているときだけ（停止・ジャンプ・落下・その場の向き替えでは出さない）。
	const UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	if (!Move || !Move->IsMovingOnGround())
		return;

	const FVector Velocity2D(Move->Velocity.X, Move->Velocity.Y, 0.0);
	const float Speed = Velocity2D.Size();
	if (Speed < MinSpeed)
		return;

	const double Now = World->GetTimeSeconds();
	if (const double* Last = LastDustTimeByFoot.Find(FootBone); Last && Now - *Last >= 0.0 && Now - *Last < MinIntervalPerFoot)
		return;

	// 足のBoneの真下に地面があるときだけ、その地面の位置から出す。
	FVector Foot = Character->GetActorLocation();
	if (!FootBone.IsNone() && MeshComp->DoesSocketExist(FootBone))
		Foot = MeshComp->GetSocketLocation(FootBone);
	else if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		Foot.Z -= Capsule->GetScaledCapsuleHalfHeight();

	FCollisionQueryParams Query(SCENE_QUERY_STAT(FootstepDust), false, Character);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Foot + FVector(0.0, 0.0, FootstepDust::GroundTraceStartOffset),
		Foot - FVector(0.0, 0.0, MaxGroundDistance), ECC_Visibility, Query))
		return;

	const FVector MoveDirection = Velocity2D.GetSafeNormal();
	const FRotator Facing(0.0f, MoveDirection.Rotation().Yaw, 0.0f);
	const FVector Location = Hit.ImpactPoint + Facing.RotateVector(LocationOffset);

	// 速度で強さを補間する（Walk→Run→Dash、通常移動→逃走）。
	const float Range = FMath::Max(FullStrengthSpeed - MinSpeed, 1.0f);
	const float Alpha = FMath::Pow(FMath::Clamp((Speed - MinSpeed) / Range, 0.0f, 1.0f), FMath::Max(StrengthExponent, 0.1f));
	const auto LerpInt = [Alpha](int32 A, int32 B) { return FMath::RoundToInt(FMath::Lerp(static_cast<float>(A), static_cast<float>(B), Alpha)); };
	const int32 SpawnCount = LerpInt(MinStrength.SpawnCount, FullStrength.SpawnCount);
	const int32 StreakCount = Speed >= StreakMinSpeed ? LerpInt(MinStrength.StreakCount, FullStrength.StreakCount) : 0;
	if (SpawnCount <= 0 && StreakCount <= 0)
		return;

	UNiagaraComponent* Dust = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, DustSystem, Location, Facing,
		FVector::OneVector, true, false, ENCPoolMethod::AutoRelease);
	if (!Dust)
		return;

	// 発生直後のBurstに効くよう、Activateの前にUser Parameterを入れる。
	Dust->SetVariableInt(TEXT("User.SpawnCount"), SpawnCount);
	Dust->SetVariableFloat(TEXT("User.SizeScale"), FMath::Lerp(MinStrength.SizeScale, FullStrength.SizeScale, Alpha));
	Dust->SetVariableFloat(TEXT("User.Lifetime"), FMath::Lerp(MinStrength.Lifetime, FullStrength.Lifetime, Alpha));
	Dust->SetVariableFloat(TEXT("User.KickSpeed"), FMath::Lerp(MinStrength.KickSpeed, FullStrength.KickSpeed, Alpha));
	Dust->SetVariableFloat(TEXT("User.KickUp"), FMath::Lerp(MinStrength.KickUp, FullStrength.KickUp, Alpha));
	Dust->SetVariableFloat(TEXT("User.Spread"), Spread);
	Dust->SetVariableLinearColor(TEXT("User.DustColor"), DustColor);
	Dust->SetVariableVec3(TEXT("User.MoveDirection"), MoveDirection);
	Dust->SetVariableInt(TEXT("User.StreakCount"), StreakCount);
	Dust->SetVariableFloat(TEXT("User.StreakLength"), StreakLength);
	Dust->SetVariableLinearColor(TEXT("User.StreakColor"), StreakColor);
	Dust->Activate(true);

	LastDustTimeByFoot.Add(FootBone, Now);
	++DustSpawnCount;
}
