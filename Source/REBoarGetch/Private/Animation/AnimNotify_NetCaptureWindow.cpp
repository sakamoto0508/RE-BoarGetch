#include "Animation/AnimNotify_NetCaptureWindow.h"

#include "Component/GadgetComponent.h"
#include "Gadget/NetGadget.h"
#include "Player/BoarPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_NetCaptureWindow::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	(void)Animation;
	(void)EventReference;

	if (MeshComp == nullptr)
	{
		return;
	}

	ABoarPlayerCharacter* PlayerCharacter = Cast<ABoarPlayerCharacter>(MeshComp->GetOwner());
	if (PlayerCharacter == nullptr)
	{
		return;
	}

	// Notify時点で装備中のガジェットがネットなら捕獲判定窓を切り替える。
	UGadgetComponent* GadgetComponent = PlayerCharacter->GetGadgetComponent();
	ANetGadget* NetGadget = GadgetComponent ? Cast<ANetGadget>(GadgetComponent->GetCurrentGadget()) : nullptr;
	
	if (NetGadget == nullptr)	return;
	
	if (bEnableCaptureWindow)
	{
		NetGadget->BeginCaptureWindow();
	}
	else
	{
		NetGadget->EndCaptureWindow();
	}
}

FString UAnimNotify_NetCaptureWindow::GetNotifyName_Implementation() const
{
	return bEnableCaptureWindow ? TEXT("Net Capture ON") : TEXT("Net Capture OFF");
}
