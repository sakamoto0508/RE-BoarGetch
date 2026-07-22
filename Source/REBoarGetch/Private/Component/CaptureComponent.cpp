#include "Component/CaptureComponent.h"
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
	if (bIsCaptured || GetOwner() == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Capture] Capture failed. Owner=%s IsCaptured=%s"),
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
	UE_LOG(LogTemp, Log, TEXT("[Capture] Captured Owner=%s Capturer=%s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(Capturer));
	return true;
}

/** 捕獲解除を実行します。 */
bool UCaptureComponent::Release()
{
	if (!bIsCaptured || GetOwner() == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Capture] Release failed. Owner=%s IsCaptured=%s"),
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
	UE_LOG(LogTemp, Log, TEXT("[Capture] Released Owner=%s"), *GetNameSafe(GetOwner()));
	return true;
}
