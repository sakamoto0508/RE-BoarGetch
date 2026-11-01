#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/BoarGameInstance.h"
#include "Gadget/GadgetBase.h"
#include "GadgetDataAsset.h"
#include "Stage/HubPortal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TextRenderActor.h"
#include "Components/TextRenderComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoarHubPortalGadgetLabelTest, "REBoarGetch.Spec.HubPortalGadgetLabels",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBoarHubPortalGadgetLabelTest::RunTest(const FString& Parameters)
{
	// 実在しないSlotを使い、ユーザーのセーブに触れない。
	auto* Instance = NewObject<UBoarGameInstance>(GEngine);
	Instance->SaveSlotName = TEXT("REBoarGetch_Automation_") + FGuid::NewGuid().ToString();
	const auto NetClass = LoadClass<AGadgetBase>(nullptr, TEXT("/Game/BP/Gadget/BP_NetGadget.BP_NetGadget_C"));
	if (!TestNotNull(TEXT("Existing catalog gadget"), NetClass))
		return false;
	const auto* Definition = NetClass->GetDefaultObject<AGadgetBase>()->GetGadgetDefinition();
	if (!TestNotNull(TEXT("Existing definition"), Definition))
		return false;
	const FName Id = Definition->GadgetId;
	const FString DisplayName = Definition->DisplayName.ToString();
	Instance->GadgetCatalog = {NetClass};
	Instance->InitializeStandalone();
	Instance->bSaveLoadFailed = true;
	UWorld* World = Instance->GetWorld();

	auto* Portal = World->SpawnActor<AHubPortal>();
	auto* LabelA = World->SpawnActor<ATextRenderActor>();
	auto* LabelB = World->SpawnActor<ATextRenderActor>();
	if (TestNotNull(TEXT("Portal"), Portal) && TestNotNull(TEXT("Label A"), LabelA) && TestNotNull(TEXT("Label B"), LabelB))
	{
		auto Entry = [](FName GadgetId, ATextRenderActor* Label)
		{
			FGadgetCollectionLabel Result;
			Result.GadgetId = GadgetId;
			Result.Label = Label;
			return Result;
		};
		auto Shows = [&](const ATextRenderActor* Label)
		{
			return !Label->IsHidden() && Label->GetTextRender()->Text.ToString().Contains(DisplayName);
		};
		Portal->GadgetLabels = {Entry(Id, LabelA)};
		Portal->RefreshTestTarget();
		TestTrue(TEXT("IDで指定したGadgetを表示"), Shows(LabelA));

		// GadgetCatalogの並びが変わっても、看板とGadgetの対応は変わらない。
		Instance->GadgetCatalog = {nullptr, NetClass};
		Portal->RefreshTestTarget();
		TestTrue(TEXT("Catalogの並び替え後も同じGadgetを表示"), Shows(LabelA));

		// 看板配列の並びが変わっても、各看板のGadgetは変わらない。未登録IDの看板は非表示。
		Portal->GadgetLabels = {Entry(TEXT("Missing"), LabelB), Entry(Id, LabelA)};
		Portal->RefreshTestTarget();
		TestTrue(TEXT("配列の並び替え後も同じGadgetを表示"), Shows(LabelA));
		TestTrue(TEXT("未登録IDの看板は非表示"), LabelB->IsHidden());

		// 不正設定の検出。正しい設定では何も報告しない。
		TestEqual(TEXT("正しい設定は問題なし"), AHubPortal::FindGadgetLabelProblems({Entry(Id, LabelA)}, Instance).Num(), 0);
		const TArray<FString> Problems = AHubPortal::FindGadgetLabelProblems({
			Entry(NAME_None, LabelB), Entry(TEXT("Missing"), nullptr), Entry(Id, LabelA), Entry(Id, LabelA)
		}, Instance);
		auto Reports = [&](const TCHAR* Index, const TCHAR* Text)
		{
			return Problems.ContainsByPredicate([&](const FString& P) { return P.StartsWith(Index) && P.Contains(Text); });
		};
		TestTrue(TEXT("ID未設定を検出"), Reports(TEXT("[0]"), TEXT("未設定")));
		TestTrue(TEXT("未登録IDを検出"), Reports(TEXT("[1]"), TEXT("存在しません")));
		TestTrue(TEXT("Label未設定を検出"), Reports(TEXT("[1]"), TEXT("Label Actorが未設定")));
		TestTrue(TEXT("ID重複を検出"), Reports(TEXT("[3]"), TEXT("重複")));
		TestTrue(TEXT("Label重複を検出"), Reports(TEXT("[3]"), TEXT("複数の要素")));
		TestEqual(TEXT("報告件数"), Problems.Num(), 5);
		TestFalse(TEXT("GIが無い場合は未登録IDを判定しない"),
		          AHubPortal::FindGadgetLabelProblems({Entry(TEXT("Missing"), LabelB)}, nullptr).Num() > 0);
	}
	Instance->Shutdown();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
