#include "Component/CaptureComponent.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UCaptureComponent::UCaptureComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/// <summary>捕獲を実行します。</summary>
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

	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Move = OwnerCharacter->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
	}

	if (AAIController* AI = Cast<AAIController>(Cast<APawn>(GetOwner()) ? Cast<APawn>(GetOwner())->GetController() : nullptr))
	{
		AI->StopMovement();
	}

	OnCaptured.Broadcast(Capturer);
	UE_LOG(LogTemp, Log, TEXT("[Capture] Captured Owner=%s Capturer=%s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(Capturer));
	return true;
}

/// <summary>捕獲解除を実行します。</summary>
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