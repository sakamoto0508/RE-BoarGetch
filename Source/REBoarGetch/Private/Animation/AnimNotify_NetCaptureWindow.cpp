#include "Animation/AnimNotify_NetCaptureWindow.h"
#include "REBoarGetch.h"

#include "Animation/ActiveMontageInstanceScope.h"
#include "Component/GadgetComponent.h"
#include "Gadget/NetGadget.h"
#include "Player/BoarPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimNotify_NetCaptureWindow::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	(void)Animation;

	if (MeshComp == nullptr)
		return;

	ABoarPlayerCharacter* PlayerCharacter = Cast<ABoarPlayerCharacter>(MeshComp->GetOwner());
	if (PlayerCharacter == nullptr)
		return;

	// Notify時点で装備中のガジェットがネットなら捕獲判定窓を切り替える。
	UGadgetComponent* GadgetComponent = PlayerCharacter->GetGadgetComponent();
	ANetGadget* NetGadget = GadgetComponent ? Cast<ANetGadget>(GadgetComponent->GetCurrentGadget()) : nullptr;
	if (NetGadget == nullptr)
		return;

	if (bEnableCaptureWindow)
	{
		// Stun・Downed・装備解除などで使用が中断された後に、止め損ねたMontageや
		// BlendOut中の古いMontageからNotifyが届いても判定窓を開かない。
		const UE::Anim::FAnimNotifyMontageInstanceContext* MontageContext =
			EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
		const int32 MontageInstanceID = MontageContext ? MontageContext->MontageInstanceID : INDEX_NONE;
		if (!PlayerCharacter->CanOpenNetCaptureWindow(NetGadget, MontageInstanceID))
		{
			UE_LOG(LogREBoarGetch, Log, TEXT("[Net] ガジェット使用中ではないため、捕獲判定窓のONを無視しました"));
			return;
		}

		NetGadget->BeginCaptureWindow();
	}
	else
	{
		// 閉じる側は使用状態に関係なく常に実行し、判定窓を残さない。
		NetGadget->EndCaptureWindow();
	}
}

FString UAnimNotify_NetCaptureWindow::GetNotifyName_Implementation() const
{
	return bEnableCaptureWindow ? TEXT("Net Capture ON") : TEXT("Net Capture OFF");
}
