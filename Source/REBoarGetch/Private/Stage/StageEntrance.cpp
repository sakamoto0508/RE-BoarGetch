#include "Stage/StageEntrance.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Stage/StageConfig.h"
#include "UI/BoarLobbyWidget.h"
#include "BoarGameInstance.h"
#include "Player/BoarPlayerController.h"
#include "UI/BoarUIFlowComponent.h"

AStageEntrance::AStageEntrance()
{
	// StageEntranceはOverlapイベントだけで動作するためTickを使用しない。
	PrimaryActorTick.bCanEverTick = false;

	// Transformの基点と、Pawnだけを検知するQuery OnlyのBoxを構築する。
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(SceneRoot);
	Trigger->SetBoxExtent(FVector(150.0f, 150.0f, 120.0f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AStageEntrance::HandleTriggerBeginOverlap);
	Trigger->OnComponentEndOverlap.AddDynamic(this, &AStageEntrance::HandleTriggerEndOverlap);
}

void AStageEntrance::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Level終了時にもViewport上のWidgetとDelegate参照を残さない。Actor単体のDestroy以外はLevel終了として扱う。
	CloseStageSelection(EndPlayReason != EEndPlayReason::Destroyed);
	Super::EndPlay(EndPlayReason);
}

void AStageEntrance::HandleTriggerBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherComp;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)SweepResult;

	// AI PawnやRemote ControllerではUIを生成せず、ローカルプレイヤーだけを対象にする。
	const APawn* Pawn = Cast<APawn>(OtherActor);
	APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!bRequiresInteraction && PlayerController && PlayerController->IsLocalController())
	{
		ShowStageSelection(PlayerController);
	}
}

bool AStageEntrance::CanInteract(const APlayerController* Controller) const
{
    return bRequiresInteraction && !LobbyWidget && !bTravelRequested && Controller &&
        Controller->IsLocalController() && Controller->GetPawn() && Trigger->IsOverlappingActor(Controller->GetPawn());
}
void AStageEntrance::Interact(APlayerController* Controller)
{
    if(CanInteract(Controller))ShowStageSelection(Controller);
}
void AStageEntrance::HandleTriggerEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	(void)OverlappedComponent;
	(void)OtherComp;
	(void)OtherBodyIndex;

	// UIを開いた本人が退出した場合だけ閉じ、別PawnのOverlap終了は無視する。
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (Pawn && Pawn->GetController() == InteractingPlayerController)
	{
		CloseStageSelection();
	}
}

void AStageEntrance::ShowStageSelection(APlayerController* PlayerController)
{
	// 必須設定不足や遷移開始後は、Widgetの生成・再表示を行わない。
	if (!PlayerController || (!StageConfig && UBoarGameInstance::ResolveStageCatalog(this, StageCatalog).IsEmpty()) || !LobbyWidgetClass || bTravelRequested)
	{
		return;
	}

	// 入力状態はUIFlowが所有する。他のメニュー表示中・Level遷移中は新しく開かない。
	auto* BoarPC = Cast<ABoarPlayerController>(PlayerController);
	UBoarUIFlowComponent* UIFlow = BoarPC ? BoarPC->GetUIFlow() : nullptr;
	if (!LobbyWidget && UIFlow && !UIFlow->CanOpenStageSelection())
	{
		return;
	}

	// 同じOverlap中はWidgetを再利用し、Delegateの重複登録と多重AddToViewportを防ぐ。
	InteractingPlayerController = PlayerController;
	if (!LobbyWidget)
	{
		LobbyWidget = CreateWidget<UBoarLobbyWidget>(PlayerController, LobbyWidgetClass);
		if (!LobbyWidget)
		{
			return;
		}

		LobbyWidget->OnStageStartRequested.AddUniqueDynamic(this, &AStageEntrance::HandleStageStartRequested);
		LobbyWidget->OnStageSelectionClosed.AddUniqueDynamic(this, &AStageEntrance::HandleStageSelectionClosed);
		LobbyWidget->AddToViewport(50);
	}

	// 表示内容を現在のStageConfigで更新してから、Game入力とUI入力を両立させる。
	TArray<UStageConfig*> Stages;
	const auto& Catalog = UBoarGameInstance::ResolveStageCatalog(this, StageCatalog);
	if (Catalog.IsEmpty()) 
		Stages.Add(StageConfig);
	else 
		for (UStageConfig* Stage : Catalog) 
			Stages.Add(Stage);
	LobbyWidget->ShowStageCatalog(Stages, StageConfig);
	// Gameplay入力の抑制・フォーカス・カーソルはUIFlowが引き受ける。
	if (UIFlow)
		UIFlow->OpenStageSelection(LobbyWidget);
}

void AStageEntrance::CloseStageSelection(bool bLevelEnding)
{
	// 破棄前にDynamic Delegateを解除し、Actor終了後のコールバックを防ぐ。
	if (LobbyWidget)
	{
		LobbyWidget->OnStageStartRequested.RemoveDynamic(this, &AStageEntrance::HandleStageStartRequested);
		LobbyWidget->OnStageSelectionClosed.RemoveDynamic(this, &AStageEntrance::HandleStageSelectionClosed);
		// Widgetを外す前に入力状態を解放させる（子画面の復帰先判定にViewport上の親が必要）。
		// Level遷移を開始した後は入力を戻さず、通常のキャンセル／退出時だけ戻す。
		const auto* BoarPC = Cast<ABoarPlayerController>(InteractingPlayerController);
		if (UBoarUIFlowComponent* UIFlow = IsValid(BoarPC) ? BoarPC->GetUIFlow() : nullptr; IsValid(UIFlow))
			UIFlow->CloseStageSelection(LobbyWidget, bLevelEnding || bTravelRequested);
		LobbyWidget->RemoveFromParent();
		LobbyWidget = nullptr;
	}
	InteractingPlayerController = nullptr;
}

void AStageEntrance::HandleStageStartRequested(UStageConfig* RequestedStageConfig)
{
	// この入口の一覧に含まれるStageだけを受理し、直接呼び出しでもLockを検証する。
	const auto& Catalog = UBoarGameInstance::ResolveStageCatalog(this, StageCatalog);
	const bool bListed = Catalog.IsEmpty() ? RequestedStageConfig == StageConfig : Catalog.Contains(RequestedStageConfig);
	if (bTravelRequested || !RequestedStageConfig || !bListed || RequestedStageConfig->Level.IsNull())
	{
		return;
	}

	// 最初の要求で即座に再入力を遮断してから、Soft World参照のLevelを開く。
	if (UBoarGameInstance* Instance = GetGameInstance<UBoarGameInstance>())
	{
		if (!Instance->IsStageUnlocked(RequestedStageConfig)) 
			return;
		if (!Instance->SaveLastAttemptedStage(RequestedStageConfig->StageId))
		{
			if (LobbyWidget) 
				LobbyWidget->ShowTravelError(NSLOCTEXT("StageSelect", "SaveFailed", "保存に失敗しました。出発を選んで再試行してください。"));
			UE_LOG(LogTemp, Error, TEXT("[StageEntrance] Last stage could not be saved; travel canceled."));
			return;
		}
	}
	else
	{
		if (LobbyWidget) 
			LobbyWidget->ShowTravelError(NSLOCTEXT("StageSelect", "NoProgress", "進行データを取得できません。"));
		return;
	}
	bTravelRequested = true;
	if (LobbyWidget)
	{
		LobbyWidget->SetIsEnabled(false);
	}
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, RequestedStageConfig->Level);
}

void AStageEntrance::HandleStageSelectionClosed()
{
	// Widget側のキャンセル操作もTrigger退出時と同じ後始末経路へ統一する。
	CloseStageSelection();
}
