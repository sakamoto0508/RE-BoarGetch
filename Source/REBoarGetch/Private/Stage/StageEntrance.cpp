#include "Stage/StageEntrance.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Stage/StageConfig.h"
#include "UI/BoarLobbyWidget.h"
#include "BoarGameInstance.h"

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
	// Level終了時にもViewport上のWidgetとDelegate参照を残さない。
	CloseStageSelection();
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
	if (PlayerController && PlayerController->IsLocalController())
	{
		ShowStageSelection(PlayerController);
	}
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
	if (!PlayerController || !StageConfig || !LobbyWidgetClass || bTravelRequested)
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
	LobbyWidget->ShowStageSelection(StageConfig);
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(LobbyWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);
}

void AStageEntrance::CloseStageSelection()
{
	// 破棄前にDynamic Delegateを解除し、Actor終了後のコールバックを防ぐ。
	if (LobbyWidget)
	{
		LobbyWidget->OnStageStartRequested.RemoveDynamic(this, &AStageEntrance::HandleStageStartRequested);
		LobbyWidget->OnStageSelectionClosed.RemoveDynamic(this, &AStageEntrance::HandleStageSelectionClosed);
		LobbyWidget->RemoveFromParent();
		LobbyWidget = nullptr;
	}

	// Level遷移中は入力状態を戻さず、通常のキャンセル／退出時だけGame Onlyへ復帰する。
	if (InteractingPlayerController && !bTravelRequested)
	{
		FInputModeGameOnly InputMode;
		InteractingPlayerController->SetInputMode(InputMode);
		InteractingPlayerController->SetShowMouseCursor(false);
	}
	InteractingPlayerController = nullptr;
}

void AStageEntrance::HandleStageStartRequested(UStageConfig* RequestedStageConfig)
{
	// 表示時に渡したものと同じStageConfigだけを受理し、Level未設定も拒否する。
	if (bTravelRequested || !RequestedStageConfig || RequestedStageConfig != StageConfig || RequestedStageConfig->Level.IsNull())
	{
		return;
	}

	// 最初の要求で即座に再入力を遮断してから、Soft World参照のLevelを開く。
	if (UBoarGameInstance* Instance = GetGameInstance<UBoarGameInstance>())
	{
		if (!Instance->IsStageUnlocked(RequestedStageConfig)) return;
		if (!Instance->SaveLastAttemptedStage(RequestedStageConfig->StageId))
		{
			UE_LOG(LogTemp, Error, TEXT("[StageEntrance] Last stage could not be saved; travel canceled."));
			return;
		}
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
