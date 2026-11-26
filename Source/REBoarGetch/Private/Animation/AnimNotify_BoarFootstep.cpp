#include "Animation/AnimNotify_BoarFootstep.h"

#include "Audio/BoarAudioManagerSubsystem.h"
#include "Boar/BoarBase.h"
#include "Component/FootstepDustComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/BoarPlayerCharacter.h"

void UAnimNotify_BoarFootstep::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// Animation Editorのプレビューなどゲーム外では鳴らさない（音量設定を持つSubsystemが無いため）。
	UWorld* World = MeshComp ? MeshComp->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || !World->GetGameInstance())
		return;

	ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner());
	if (!Character)
		return;

	// BlendSpaceの重なりで空中に残ったNotifyは鳴らさない。
	if (const UCharacterMovementComponent* Move = Character->GetCharacterMovement(); Move && !Move->IsMovingOnGround())
		return;

	// 走行の土煙（演出のみ）。足音の有無・音量設定とは独立に、所有者のComponentが条件（速度・種別・地面）を判定する。
	if (UFootstepDustComponent* Dust = Character->FindComponentByClass<UFootstepDustComponent>())
		Dust->HandleFootstep(MeshComp, FootBone);

	USoundBase* Sound = nullptr;
	float Volume = 1.0f;
	USoundAttenuation* Attenuation = nullptr;

	if (const auto* Player = Cast<ABoarPlayerCharacter>(Character))
	{
		Sound = Player->GetPlayerAudio().Footstep;
		Volume = Player->GetPlayerAudio().FootstepVolume;
		Attenuation = Player->GetPlayerAudio().Attenuation;
	}
	else if (const auto* Boar = Cast<ABoarBase>(Character))
	{
		if (Boar->IsCaptured())
			return;

		Sound = Boar->GetAudioSettings().Footstep;
		Volume = Boar->GetAudioSettings().FootstepVolume;
		Attenuation = Boar->GetAudioSettings().Attenuation;
	}

	if (!Sound)
		return;

	FVector Location = Character->GetActorLocation();
	if (!FootBone.IsNone() && MeshComp->DoesSocketExist(FootBone))
		Location = MeshComp->GetSocketLocation(FootBone);
	else if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		Location.Z -= Capsule->GetScaledCapsuleHalfHeight();

	if (auto* Audio = World->GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>())
		Audio->PlayWorldSoundEffect(Character, Sound, Location, nullptr, Attenuation, Volume * VolumeMultiplier);
}

FString UAnimNotify_BoarFootstep::GetNotifyName_Implementation() const
{
	return FootBone.IsNone() ? TEXT("Footstep") : FString::Printf(TEXT("Footstep (%s)"), *FootBone.ToString());
}
