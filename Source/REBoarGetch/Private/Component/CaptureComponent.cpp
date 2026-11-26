#include "Component/CaptureComponent.h"
#include "REBoarGetch.h"
#include "Core/BoarGameMode.h"
#include "Engine/World.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UCaptureComponent::UCaptureComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/** 捕獲を実行します。 */
bool UCaptureComponent::Capture(AActor* Capturer)
{
	if (!ABoarGameMode::IsStageProgressAllowed(this))
		return false;
	if (bIsCaptured || GetOwner() == nullptr)
	{
		UE_LOG(LogREBoarGetch, Warning, TEXT("[Capture] 捕獲に失敗しました。Owner=%s IsCaptured=%s"),
			*GetNameSafe(GetOwner()),
			bIsCaptured ? TEXT("true") : TEXT("false"));
		return false;
	}

	bIsCaptured = true;

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (ACharacter* OwnerCharacter = Cast<ACharacter>(OwnerPawn))
	{
		if (UCharacterMovementComponent* Move = OwnerCharacter->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
	}

	if (AAIController* AI = OwnerPawn ? Cast<AAIController>(OwnerPawn->GetController()) : nullptr)
	{
		AI->StopMovement();
	}

	OnCaptured.Broadcast(Capturer);
	UE_LOG(LogREBoarGetch, Log, TEXT("[Capture] 捕獲しました。Owner=%s Capturer=%s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(Capturer));
	return true;
}

/** 捕獲解除を実行します。 */
bool UCaptureComponent::Release()
{
	if (!ABoarGameMode::IsStageProgressAllowed(this))
		return false;
	if (!bIsCaptured || GetOwner() == nullptr)
	{
		UE_LOG(LogREBoarGetch, Warning, TEXT("[Capture] 解放に失敗しました。Owner=%s IsCaptured=%s"),
			*GetNameSafe(GetOwner()),
			bIsCaptured ? TEXT("true") : TEXT("false"));
		return false;
	}

	bIsCaptured = false;

	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Move = OwnerCharacter->GetCharacterMovement())
		{
			Move->SetMovementMode(MOVE_Walking);
		}
	}

	OnReleased.Broadcast();
	UE_LOG(LogREBoarGetch, Log, TEXT("[Capture] 解放しました。Owner=%s"), *GetNameSafe(GetOwner()));
	return true;
}
