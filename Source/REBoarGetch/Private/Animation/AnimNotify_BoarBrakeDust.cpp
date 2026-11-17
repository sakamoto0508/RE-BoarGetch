#include "Animation/AnimNotify_BoarBrakeDust.h"

#include "Boar/BoarBase.h"
#include "Core/BoarGameMode.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
	/** Meshごとの最後の発生時刻（連続生成の抑制用）。弱参照なので破棄されたMeshは自然に無効になる。 */
	TMap<TWeakObjectPtr<const USkeletalMeshComponent>, double> GLastDustTime;
}

UAnimNotify_BoarBrakeDust::UAnimNotify_BoarBrakeDust()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(176, 128, 82);
#endif
}

FString UAnimNotify_BoarBrakeDust::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("BrakeDust %s"), *FootBone.ToString());
}

void UAnimNotify_BoarBrakeDust::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (MeshComp == nullptr || DustSystem == nullptr)
		return;
	UWorld* World = MeshComp->GetWorld();
	if (World == nullptr)
		return;

	// ゲーム中は捕獲済み・ステージ進行外（Capture演出・クリア・GameOver・中断）では出さない。エディタのプレビューでは常に出す。
	AActor* Owner = MeshComp->GetOwner();
	if (World->IsGameWorld())
	{
		if (const ABoarBase* Boar = Cast<ABoarBase>(Owner); Boar != nullptr && Boar->IsCaptured())
			return;
		if (const ABoarGameMode* Mode = World->GetAuthGameMode<ABoarGameMode>(); Mode != nullptr && !Mode->CanAdvanceStage())
			return;
	}

	const double Now = World->GetTimeSeconds();
	if (const double* Last = GLastDustTime.Find(MeshComp); Last != nullptr && Now - *Last >= 0.0 && Now - *Last < MinInterval)
		return;

	// 足のBoneの真下に地面があるときだけ、その地面の位置から出す（空中では出さない）。
	const FVector Foot = MeshComp->DoesSocketExist(FootBone) ? MeshComp->GetSocketLocation(FootBone) : MeshComp->GetComponentLocation();
	const FRotator Facing(0.0f, Owner ? Owner->GetActorRotation().Yaw : MeshComp->GetComponentRotation().Yaw, 0.0f);
	FVector SpawnLocation = Foot;
	if (World->IsGameWorld())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BoarBrakeDust), false, Owner);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Foot + FVector(0.0f, 0.0f, 10.0f), Foot - FVector(0.0f, 0.0f, MaxGroundDistance),
			ECC_Visibility, Params))
			return;
		SpawnLocation = Hit.ImpactPoint;
	}
	SpawnLocation += Facing.RotateVector(LocationOffset);

	UNiagaraComponent* Dust = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, DustSystem, SpawnLocation, Facing,
		FVector(Scale), true, false, ENCPoolMethod::AutoRelease);
	if (Dust == nullptr)
		return;
	// 発生直後のBurstに効くよう、Activateの前にUser Parameterを上書きする。
	if (bOverrideParameters)
	{
		Dust->SetVariableInt(TEXT("User.SpawnCount"), SpawnCount);
		Dust->SetVariableFloat(TEXT("User.SizeScale"), SizeScale);
		Dust->SetVariableFloat(TEXT("User.Lifetime"), Lifetime);
		Dust->SetVariableLinearColor(TEXT("User.DustColor"), DustColor);
	}
	Dust->Activate(true);
	GLastDustTime.Add(MeshComp, Now);
	// 破棄されたMeshの記録を時々掃除する。
	if (GLastDustTime.Num() > 64)
	{
		for (auto It = GLastDustTime.CreateIterator(); It; ++It)
			if (!It.Key().IsValid())
				It.RemoveCurrent();
	}
}
