#include "Presentation/BoarPresentationDirector.h"
#include "Audio/BoarAudioManagerSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Player/BoarPlayerCharacter.h"
#include "Player/BoarPlayerController.h"

ABoarPresentationDirector::ABoarPresentationDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);
}

void ABoarPresentationDirector::BeginPresentation(ABoarPlayerController* InController, FSimpleDelegate InOnFinished)
{
	Controller = InController;
	OnFinished = MoveTemp(InOnFinished);
	LastRealTime = GetWorld()->GetRealTimeSeconds();
	OnPresentationStarted();
}

void ABoarPresentationDirector::FinishPresentation()
{
	if (bFinished) return;
	bFinished = true;
	UE_LOG(LogTemp, Log, TEXT("[Presentation] %s finished after %.2fs"), *GetClass()->GetName(), Age);
	OnFinished.ExecuteIfBound();
}

ABoarPlayerController* ABoarPresentationDirector::GetPresentationController() const
{
	return Controller.Get();
}

ABoarPlayerCharacter* ABoarPresentationDirector::GetPresentationPlayer() const
{
	return Controller.IsValid() ? Cast<ABoarPlayerCharacter>(Controller->GetPawn()) : nullptr;
}

void ABoarPresentationDirector::PlayPresentationSound(USoundBase* Sound, float Pitch)
{
	if (Sound && GetGameInstance())
		if (auto* Audio = GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->PlaySoundEffect2D(this, Sound, 1.f, Pitch);
}

void ABoarPresentationDirector::PlayPresentationBGM(USoundBase* Music, float FadeInTime)
{
	if (Music && GetGameInstance())
		if (auto* Audio = GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->PlayBGM(this, Music, FadeInTime, true);
}

void ABoarPresentationDirector::StopStageBGM(float FadeOutTime)
{
	if (GetGameInstance())
		if (auto* Audio = GetGameInstance()->GetSubsystem<UBoarAudioManagerSubsystem>()) Audio->StopBGM(FadeOutTime);
}

void ABoarPresentationDirector::SetPlayerHUDHidden(bool bHideHUD)
{
	if (Controller.IsValid()) Controller->SetPlayerHUDHidden(bHideHUD);
}

void ABoarPresentationDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetRealTimeSeconds();
	const float Dt = FMath::Clamp(float(Now - LastRealTime), 0.f, .1f);
	LastRealTime = Now;
	Age += Dt;
	// ヒットストップ中もBPのDelay / Timelineが実時間で進むよう、Global Time Dilationを打ち消します。
	CustomTimeDilation = 1.f / FMath::Max(UGameplayStatics::GetGlobalTimeDilation(this), .0001f);
	TickPresentation(Dt);
}
