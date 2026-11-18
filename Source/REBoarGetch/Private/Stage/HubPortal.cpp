#include "Stage/HubPortal.h"
#include "Components/BoxComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Player/BoarPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "Core/BoarGameInstance.h"
#include "Gadget/GadgetBase.h"
#include "GadgetDataAsset.h"
#include "Engine/TextRenderActor.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/TextRenderComponent.h"
#include "Stage/BoarLevelDestination.h"
#include "Stage/BoarLevelTransitionLibrary.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	const FName TestVisualTag(TEXT("PortalTestVisual"));
	const FName TestBarrierTag(TEXT("PortalTestBarrier"));
	const FName TestLabelTag(TEXT("PortalTestLabel"));
	const FName TestPreviewTag(TEXT("PortalTestPreview"));

	FString GetTravelPackage(const TSoftObjectPtr<UWorld>& Destination)
	{
		return UWorld::RemovePIEPrefix(Destination.ToSoftObjectPath().GetLongPackageName());
	}

	bool HasTravelMap(const FString& PackageName)
	{
		return !PackageName.IsEmpty() && FPackageName::DoesPackageExist(PackageName);
	}
}

AHubPortal::AHubPortal()
{
	PrimaryActorTick.bCanEverTick = false;
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("PortalTrigger"));
	SetRootComponent(Trigger);
	Trigger->SetBoxExtent(FVector(140, 80, 150));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetCanEverAffectNavigation(false);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AHubPortal::Enter);
	Transition.Effect = EACFTransitionEffect::DigitalGlitch;
}

void AHubPortal::Enter(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	const APawn* Pawn = Cast<APawn>(Other);
	auto* PC = Pawn ? Cast<ABoarPlayerController>(Pawn->GetController()) : nullptr;
	if (!bRequiresInteraction)
		Activate(PC);
}

void AHubPortal::Activate(ABoarPlayerController* PC)
{
	if (!PC || !PC->IsLocalController() || bTravelRequested)
		return;
	TSoftObjectPtr<UWorld> TravelDestination = GetTravelDestination();
	if (Action == EHubPortalAction::GadgetTest)
	{
		auto* GI = GetGameInstance<UBoarGameInstance>();
		if (!GI || !GI->IsInGadgetLab())
			return;
		TravelDestination = GI->GetSelectedTestLevel();
	}
	else if (Action != EHubPortalAction::Travel)
	{
		PC->OpenFacilityMenu(Action == EHubPortalAction::Archive);
		return;
	}
	// PIE rewrites soft references to visited worlds. Validate and travel using
	// the on-disk package, never the transient UEDPIE_<instance>_ package.
	const FString PackageName = GetTravelPackage(TravelDestination);
	if (!HasTravelMap(PackageName))
	{
		UE_LOG(LogTemp, Error, TEXT("[HubPortal] %s: invalid Level destination '%s'"),
		       *GetActorNameOrLabel(), *TravelDestination.ToString());
		return;
	}
	bTravelRequested = true;
	if (bClearTestTargetOnTravel)
		if (auto* GI = GetGameInstance<UBoarGameInstance>())
			GI->ClearTestGadget();
	const FBoarLevelTransitionEntry& Effect = bOverrideTransition
		? Transition
		: GetDefault<UBoarLevelTransitionSettings>()->GetEntry(TransitionStyle);
	UBoarLevelTransitionLibrary::OpenLevelWithTransitionEntry(this, FName(*PackageName), Effect);
}

TSoftObjectPtr<UWorld> AHubPortal::GetTravelDestination() const
{
	const AHubPortal* Defaults = GetClass()->GetDefaultObject<AHubPortal>();
	if (Defaults->bUseClassDestination)
		return Defaults->Destination;
	return DestinationAsset ? DestinationAsset->Level : Destination;
}

void AHubPortal::BeginPlay()
{
	Super::BeginPlay();
	if (Action == EHubPortalAction::GadgetTest)
	{
		if (auto* GI = GetGameInstance<UBoarGameInstance>())
		{
			// 不正な看板設定はクラッシュさせず、該当看板を非表示にしたうえで原因をログへ出す。
			for (const FString& Problem : FindGadgetLabelProblems(GadgetLabels, GI))
				UE_LOG(LogTemp, Warning, TEXT("[HubPortal] %s GadgetLabels: %s"), *GetActorNameOrLabel(), *Problem);
			GI->OnTestGadgetChanged.AddUniqueDynamic(this, &AHubPortal::RefreshTestTarget);
			GI->OnGadgetsUnlocked.AddUniqueDynamic(this, &AHubPortal::RefreshTestTarget);
		}
		RefreshTestTarget();
	}
}

void AHubPortal::Interact(ABoarPlayerController* Controller)
{
	if (!bRequiresInteraction || !Controller || !Controller->IsLocalController() ||
		!Controller->GetPawn() || !Trigger->IsOverlappingActor(Controller->GetPawn()))
		return;
	if (Action == EHubPortalAction::Loadout || Action == EHubPortalAction::Archive)
		Controller->OpenFacilityMenu(Action == EHubPortalAction::Archive);
	else 
		Activate(Controller);
}

void AHubPortal::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* GI = GetGameInstance<UBoarGameInstance>())
	{
		GI->OnTestGadgetChanged.RemoveDynamic(this, &AHubPortal::RefreshTestTarget);
		GI->OnGadgetsUnlocked.RemoveDynamic(this, &AHubPortal::RefreshTestTarget);
	}
	Super::EndPlay(Reason);
}

void AHubPortal::RefreshTestTarget()
{
	auto* GI = GetGameInstance<UBoarGameInstance>();
	for (const FGadgetCollectionLabel& Entry : GadgetLabels)
	{
		auto* Label = Entry.Label.Get();
		if (!Label)
			continue;
		// 配列Indexではなく、看板に設定したGadgetIdからGadgetを解決する。
		const auto Class = GI ? GI->FindGadgetClass(Entry.GadgetId) : nullptr;
		const auto* Def = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
		Label->SetActorHiddenInGame(!Def);
		if (Def)Label->GetTextRender()->SetText(FText::Format(FText::FromString(TEXT("{0}  {1}")),
		                                                      FText::FromString(
			                                                      GI->IsGadgetUnlocked(Def->GadgetId)
				                                                      ? TEXT("READY")
				                                                      : TEXT("LOCKED")), Def->DisplayName));
	}
	const bool bShow = GI && GI->IsInGadgetLab() && !GI->GetSelectedTestGadget().IsNone() && GI->IsGadgetUnlocked(
		GI->GetSelectedTestGadget());
	SetActorHiddenInGame(false);
	const TSoftObjectPtr<UWorld> TestLevel = bShow ? GI->GetSelectedTestLevel() : TSoftObjectPtr<UWorld>();
	const bool bHasLevel = bShow && HasTravelMap(GetTravelPackage(TestLevel));
	if (bShow && !TestLevel.IsNull() && !bHasLevel)
		UE_LOG(LogTemp, Warning, TEXT("[HubPortal] %s: invalid Gadget Test Level '%s'"),
		       *GetActorNameOrLabel(), *TestLevel.ToString());
	for (const auto& A : TestVisualActors)
		if (A)A->SetActorHiddenInGame(!bHasLevel);
	for (const auto& A : TestBarrierActors)
		if (A)
		{
			A->SetActorHiddenInGame(bHasLevel);
			A->SetActorEnableCollision(!bHasLevel);
		}
	const auto Class = GI ? GI->FindGadgetClass(GI->GetSelectedTestGadget()) : nullptr;
	const auto* Def = Class ? Class.GetDefaultObject()->GetGadgetDefinition() : nullptr;
	const FText LabelText = Def
		? FText::Format(NSLOCTEXT("HubPortal", "TestTarget", "TEST : {0}{1}"), Def->DisplayName,
		                bHasLevel ? FText::GetEmpty()
		                          : NSLOCTEXT("HubPortal", "TestLevelMissing", " - Level未登録"))
		: NSLOCTEXT("HubPortal", "SelectTestTarget", "SELECT GADGET TO TEST");
	if (TestLabel)
	{
		TestLabel->SetActorHiddenInGame(false);
		TestLabel->GetTextRender()->SetText(LabelText);
	}
	for (UPrimitiveComponent* Part : TInlineComponentArray<UPrimitiveComponent*>(this))
	{
		if (Part->ComponentHasTag(TestVisualTag))
			Part->SetHiddenInGame(!bHasLevel);
		if (Part->ComponentHasTag(TestBarrierTag))
		{
			Part->SetHiddenInGame(bHasLevel);
			Part->SetCollisionEnabled(bHasLevel ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
		}
		if (Part->ComponentHasTag(TestLabelTag))
			if (auto* Label = Cast<UTextRenderComponent>(Part))
				Label->SetText(LabelText);
	}
	// Enable the overlap only after the barrier has been opened.
	RefreshTestPreview(bShow ? GI->GetSelectedTestGadget() : NAME_None);
	Trigger->SetCollisionEnabled(bHasLevel ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void AHubPortal::RefreshTestPreview(FName GadgetId)
{
	const TSubclassOf<ASkeletalMeshActor>* PreviewClass = TestPreviewClasses.Find(GadgetId);
	for (UChildActorComponent* Component : TInlineComponentArray<UChildActorComponent*>(this))
	{
		if (!Component->ComponentHasTag(TestPreviewTag))
			continue;
		Component->SetChildActorClass(PreviewClass ? PreviewClass->Get() : nullptr);
		if (auto* Preview = Cast<ASkeletalMeshActor>(Component->GetChildActor()))
		{
			Preview->SetActorEnableCollision(false);
			USkeletalMeshComponent* Mesh = Preview->GetSkeletalMeshComponent();
			Mesh->SetCastShadow(false);
			if (TestPreviewMaterial)
				for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
					Mesh->SetMaterial(Slot, TestPreviewMaterial);
		}
	}
}

TArray<FString> AHubPortal::FindGadgetLabelProblems(const TArray<FGadgetCollectionLabel>& Labels,
                                                    const UBoarGameInstance* GI)
{
	TArray<FString> Problems;
	TSet<FName> SeenIds;
	TSet<const ATextRenderActor*> SeenLabels;
	for (int32 I = 0; I < Labels.Num(); ++I)
	{
		const FGadgetCollectionLabel& Entry = Labels[I];
		if (Entry.GadgetId.IsNone())
			Problems.Add(FString::Printf(TEXT("[%d] GadgetIdが未設定です"), I));
		else
		{
			if (GI && !GI->FindGadgetClass(Entry.GadgetId))
				Problems.Add(FString::Printf(TEXT("[%d] GadgetId '%s' がGadgetCatalogに存在しません"), I,
				                             *Entry.GadgetId.ToString()));
			bool bDuplicateId = false;
			SeenIds.Add(Entry.GadgetId, &bDuplicateId);
			if (bDuplicateId)
				Problems.Add(FString::Printf(TEXT("[%d] GadgetId '%s' が重複しています"), I, *Entry.GadgetId.ToString()));
		}
		if (!Entry.Label)
			Problems.Add(FString::Printf(TEXT("[%d] Label Actorが未設定です"), I));
		else
		{
			bool bDuplicateLabel = false;
			SeenLabels.Add(Entry.Label.Get(), &bDuplicateLabel);
			if (bDuplicateLabel)
				Problems.Add(FString::Printf(TEXT("[%d] Label Actor '%s' が複数の要素に設定されています"), I,
				                             *Entry.Label->GetActorNameOrLabel()));
		}
	}
	return Problems;
}

#if WITH_EDITOR
EDataValidationResult AHubPortal::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	// EditorではGameInstanceが無いため、未登録IDの判定はPIE開始時のログで行う。
	for (const FString& Problem : FindGadgetLabelProblems(GadgetLabels, nullptr))
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s GadgetLabels: %s"), *GetActorNameOrLabel(), *Problem)));
		Result = EDataValidationResult::Invalid;
	}
	const AHubPortal* Defaults = GetClass()->GetDefaultObject<AHubPortal>();
	if (Action == EHubPortalAction::Travel && (!IsTemplate() || Defaults->bUseClassDestination)
		&& !HasTravelMap(GetTravelPackage(GetTravelDestination())))
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: 有効な遷移先 Level が設定されていません"), *GetActorNameOrLabel())));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
