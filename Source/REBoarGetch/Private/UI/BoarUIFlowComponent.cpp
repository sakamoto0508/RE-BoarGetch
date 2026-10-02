#include "UI/BoarUIFlowComponent.h"
#include "Player/BoarPlayerController.h"
#include "Player/BoarPlayerCharacter.h"
#include "Core/BoarGameMode.h"
#include "Core/BoarFacilityGameMode.h"
#include "UI/BoarPauseWidget.h"
#include "UI/BoarLoadoutWidget.h"
#include "UI/BoarSettingsWidget.h"
#include "UI/BoarConfirmationWidget.h"
#include "UI/BoarEncyclopediaWidget.h"
#include "UI/BoarArchiveSelectionWidget.h"
#include "UI/BoarLobbyWidget.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

ABoarPlayerController *UBoarUIFlowComponent::GetController() const
{
	return Cast<ABoarPlayerController>(GetOwner());
}
bool UBoarUIFlowComponent::BlocksGameplayInput() const
{
	return bFacilityMenuOpen || PauseWidget || SettingsWidget || EncyclopediaWidget || ArchiveSelectionWidget;
}
void UBoarUIFlowComponent::Shutdown()
{
	if (bFacilityMenuOpen)
		CloseLoadoutMenu();
	CloseArchiveDisplaySelection();
	CloseEncyclopedia();
	CloseSettingsMenu();
	ResumeFromPause();
}
void UBoarUIFlowComponent::TogglePauseMenu()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (PauseWidget || SettingsWidget || EncyclopediaWidget || ArchiveSelectionWidget)
		HandleMenuBack();
	else
		OpenPauseMenu();
}

void UBoarUIFlowComponent::HandleMenuBack()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (ArchiveSelectionWidget)
	{
		CloseArchiveDisplaySelection();
		return;
	}
	if (EncyclopediaWidget)
		CloseEncyclopedia();
	else if (LobbyExitConfirmation)
		ResolveLobbyExit(false);
	else if (SettingsWidget)
		CloseSettingsMenu();
	else if (LoadoutWidget)
		CloseLoadoutMenu();
	else
		ResumeFromPause();
}

void UBoarUIFlowComponent::OpenLoadoutMenu()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!PauseWidget || LoadoutWidget || SettingsWidget || LobbyExitConfirmation || !PC->LoadoutWidgetClass ||
		PC->bLevelTransitionRequested)
		return;
	LoadoutWidget = CreateWidget<UBoarLoadoutWidget>(PC, PC->LoadoutWidgetClass);
	if (!LoadoutWidget)
		return;
	LoadoutWidget->OnClosed.AddUniqueDynamic(this, &UBoarUIFlowComponent::CloseLoadoutMenu);
	PauseWidget->SetVisibility(ESlateVisibility::Collapsed);
	LoadoutWidget->AddToViewport(90);
	// 既存UI入力ContextとPauseを維持し、戻る操作で親画面へ戻します。
	FInputModeGameAndUI ModeUI;
	ModeUI.SetHideCursorDuringCapture(false);
	ModeUI.SetWidgetToFocus(LoadoutWidget->TakeWidget());
	ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(ModeUI);
	LoadoutWidget->FocusInitialChoice();
}

void UBoarUIFlowComponent::OpenEncyclopedia(UUserWidget *ParentMenu, UWidget *ReturnFocus)
{
	auto *PC = GetController();
	if (!PC)
		return;
	const auto *ArchiveMode = GetWorld()->GetAuthGameMode<ABoarFacilityGameMode>();
	if (!ArchiveMode || !ArchiveMode->bArchive)
		return;
	if (!PC->IsLocalController() || !ParentMenu || EncyclopediaWidget || !PC->EncyclopediaWidgetClass || LoadoutWidget ||
		LobbyExitConfirmation || PC->bLevelTransitionRequested)
		return;
	if (ParentMenu != SettingsWidget && !Cast<UBoarLobbyWidget>(ParentMenu))
		return;
	EncyclopediaWidget = CreateWidget<UBoarEncyclopediaWidget>(PC, PC->EncyclopediaWidgetClass);
	if (!EncyclopediaWidget)
		return;
	EncyclopediaParent = ParentMenu;
	EncyclopediaReturnFocus = ReturnFocus;
	EncyclopediaParentVisibility = ParentMenu->GetVisibility();
	bEncyclopediaGameAndUI = PauseWidget || Cast<UBoarLobbyWidget>(ParentMenu);
	bEncyclopediaAddedUIContext = PC->UIMappingContext && !PC->OwnedMappingContexts.Contains(PC->UIMappingContext);
	if (bEncyclopediaAddedUIContext)
		PC->AddOwnedMappingContext(PC->UIMappingContext, 20);
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);
	if (auto *PlayerCharacter = PC->GetBoarCharacter())
	{
		PlayerCharacter->StopDash();
		PlayerCharacter->StopJump();
		PlayerCharacter->StopGadgetUse();
		PlayerCharacter->GetCharacterMovement()->StopMovementImmediately();
		PlayerCharacter->SetMenuOpen(true);
	}
	EncyclopediaWidget->OnClosed.AddUniqueDynamic(this, &UBoarUIFlowComponent::CloseEncyclopedia);
	ParentMenu->SetVisibility(ESlateVisibility::Collapsed);
	EncyclopediaWidget->AddToViewport(120);
	FInputModeGameAndUI ModeUI;
	ModeUI.SetWidgetToFocus(EncyclopediaWidget->TakeWidget());
	ModeUI.SetHideCursorDuringCapture(false);
	ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(ModeUI);
	PC->SetShowMouseCursor(true);
	EncyclopediaWidget->FocusInitialChoice();
}

void UBoarUIFlowComponent::CloseEncyclopedia()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!EncyclopediaWidget)
		return;
	EncyclopediaWidget->OnClosed.RemoveDynamic(this, &UBoarUIFlowComponent::CloseEncyclopedia);
	EncyclopediaWidget->RemoveFromParent();
	EncyclopediaWidget = nullptr;
	if (bFacilityMenuOpen)
	{
		CloseFacilityMenuInput();
		return;
	}
	if (bEncyclopediaAddedUIContext)
		PC->RemoveOwnedMappingContext(PC->UIMappingContext);
	bEncyclopediaAddedUIContext = false;
	PC->SetIgnoreMoveInput(false);
	PC->SetIgnoreLookInput(false);
	if (!PauseWidget)
		if (auto *PlayerCharacter = PC->GetBoarCharacter())
			PlayerCharacter->SetMenuOpen(false);
	PC->FlushPressedKeys();
	if (IsValid(EncyclopediaParent) && EncyclopediaParent->IsInViewport())
	{
		EncyclopediaParent->SetVisibility(EncyclopediaParentVisibility);
		if (bEncyclopediaGameAndUI)
		{
			FInputModeGameAndUI ModeUI;
			ModeUI.SetWidgetToFocus(EncyclopediaParent->TakeWidget());
			ModeUI.SetHideCursorDuringCapture(false);
			ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(ModeUI);
		}
		else
		{
			FInputModeUIOnly ModeUI;
			ModeUI.SetWidgetToFocus(EncyclopediaParent->TakeWidget());
			ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(ModeUI);
		}
		if (IsValid(EncyclopediaReturnFocus))
			EncyclopediaReturnFocus->SetUserFocus(PC);
	}
	EncyclopediaParent = nullptr;
	EncyclopediaReturnFocus = nullptr;
}

void UBoarUIFlowComponent::CloseEncyclopediaFrom(UUserWidget *ParentMenu)
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (EncyclopediaParent == ParentMenu)
		CloseEncyclopedia();
}

void UBoarUIFlowComponent::OpenSettingsMenu(UUserWidget *ParentMenu, UWidget *ReturnFocus)
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!PC->IsLocalController() || !ParentMenu || SettingsWidget || LoadoutWidget || LobbyExitConfirmation || !PC->SettingsWidgetClass ||
		PC->bLevelTransitionRequested)
		return;
	SettingsWidget = CreateWidget<UBoarSettingsWidget>(PC, PC->SettingsWidgetClass);
	if (!SettingsWidget)
		return;
	SettingsParent = ParentMenu;
	SettingsReturnFocus = ReturnFocus;
	SettingsParentVisibility = ParentMenu->GetVisibility();
	SettingsWidget->OnClosed.AddUniqueDynamic(this, &UBoarUIFlowComponent::CloseSettingsMenu);
	ParentMenu->SetVisibility(ESlateVisibility::Collapsed);
	SettingsWidget->AddToViewport(100);
	if (PauseWidget)
	{
		FInputModeGameAndUI ModeUI;
		ModeUI.SetWidgetToFocus(SettingsWidget->TakeWidget());
		ModeUI.SetHideCursorDuringCapture(false);
		ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(ModeUI);
	}
	else
	{
		FInputModeUIOnly ModeUI;
		ModeUI.SetWidgetToFocus(SettingsWidget->TakeWidget());
		ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(ModeUI);
	}
	PC->SetShowMouseCursor(true);
	SettingsWidget->FocusInitialChoice();
}

void UBoarUIFlowComponent::CloseSettingsMenu()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!SettingsWidget)
		return;
	CloseEncyclopedia();
	SettingsWidget->OnClosed.RemoveDynamic(this, &UBoarUIFlowComponent::CloseSettingsMenu);
	SettingsWidget->RemoveFromParent();
	SettingsWidget = nullptr;
	if (IsValid(SettingsParent))
	{
		SettingsParent->SetVisibility(SettingsParentVisibility);
		if (PauseWidget)
		{
			FInputModeGameAndUI ModeUI;
			ModeUI.SetWidgetToFocus(SettingsParent->TakeWidget());
			ModeUI.SetHideCursorDuringCapture(false);
			ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(ModeUI);
		}
		else
		{
			FInputModeUIOnly ModeUI;
			ModeUI.SetWidgetToFocus(SettingsParent->TakeWidget());
			ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(ModeUI);
		}
		if (IsValid(SettingsReturnFocus))
			SettingsReturnFocus->SetUserFocus(PC);
	}
	SettingsParent = nullptr;
	SettingsReturnFocus = nullptr;
}

void UBoarUIFlowComponent::CloseLoadoutMenu()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!LoadoutWidget)
		return;
	LoadoutWidget->OnClosed.RemoveDynamic(this, &UBoarUIFlowComponent::CloseLoadoutMenu);
	LoadoutWidget->RemoveFromParent();
	LoadoutWidget = nullptr;
	if (bFacilityMenuOpen)
	{
		CloseFacilityMenuInput();
		return;
	}
	if (PauseWidget)
	{
		PauseWidget->SetVisibility(ESlateVisibility::Visible);
		PauseWidget->FocusLoadoutChoice();
	}
}

void UBoarUIFlowComponent::OpenPauseMenu()
{
	auto *PC = GetController();
	if (!PC)
		return;
	const auto *Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ABoarGameMode>() : nullptr;
	// Lobby/Title、終了演出、他のPauseとの競合、および多重生成を防ぎます。
	if (!PC->IsLocalController() || PauseWidget || !PC->PauseWidgetClass || PC->bGameplayInputBlocked || PC->bLevelTransitionRequested ||
		!Mode || !Mode->CanAdvanceStage() || UGameplayStatics::IsGamePaused(this))
		return;
	auto *Widget = CreateWidget<UBoarPauseWidget>(PC, PC->PauseWidgetClass);
	if (!Widget)
		return;
	if (!PC->SetPause(true))
		return;
	PauseWidget = Widget;
	// 旧BPは入口の判定で保護し、専用Context設定済みの画面だけ切り替えます。
	if (PC->GlobalMappingContext && PC->UIMappingContext)
	{
		PC->RemoveOwnedMappingContext(PC->DefaultMappingContext);
		PC->AddOwnedMappingContext(PC->UIMappingContext, 20);
	}
	// 押下状態を持ち越さず、既存のアクション終了経路を使用します。
	PC->bIsGadgetModifierHeld = false;
	if (auto *PlayerCharacter = PC->GetBoarCharacter())
	{
		PlayerCharacter->StopDash();
		PlayerCharacter->StopJump();
		PlayerCharacter->StopGadgetUse();
		PlayerCharacter->SetMenuOpen(true);
	}
	PC->FlushPressedKeys();
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);
	Widget->AddToViewport(80);
	FInputModeGameAndUI ModeUI;
	ModeUI.SetHideCursorDuringCapture(false);
	ModeUI.SetWidgetToFocus(Widget->TakeWidget());
	ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(ModeUI);
	PC->SetShowMouseCursor(true);
	Widget->FocusInitialChoice();
}

void UBoarUIFlowComponent::ResumeFromPause()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!PauseWidget)
		return;
	CloseEncyclopedia();
	RemoveLobbyExitConfirmation();
	CloseSettingsMenu();
	CloseLoadoutMenu();
	PC->SetPause(false);
	PauseWidget->RemoveFromParent();
	PauseWidget = nullptr;
	PC->RemoveOwnedMappingContext(PC->UIMappingContext);
	PC->AddOwnedMappingContext(PC->DefaultMappingContext, 0);
	if (auto *PlayerCharacter = PC->GetBoarCharacter())
		PlayerCharacter->SetMenuOpen(false);
	PC->SetIgnoreMoveInput(false);
	PC->SetIgnoreLookInput(false);
	PC->FlushPressedKeys();
	FInputModeGameOnly ModeGame;
	PC->SetInputMode(ModeGame);
	PC->SetShowMouseCursor(false);
}

void UBoarUIFlowComponent::LeaveStageFromPause()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!PauseWidget || SettingsWidget || LoadoutWidget || LobbyExitConfirmation || !PC->LobbyExitConfirmationClass ||
		(PC->LobbyLevel.IsNull() && PC->LobbyLevelName.IsNone()) || PC->bLevelTransitionRequested)
		return;
	LobbyExitConfirmation = CreateWidget<UBoarConfirmationWidget>(PC, PC->LobbyExitConfirmationClass);
	if (!LobbyExitConfirmation)
		return;
	LobbyExitConfirmation->OnDecision.AddUniqueDynamic(this, &UBoarUIFlowComponent::ResolveLobbyExit);
	PauseWidget->SetVisibility(ESlateVisibility::Collapsed);
	LobbyExitConfirmation->AddToViewport(110);
	FInputModeGameAndUI ModeUI;
	ModeUI.SetWidgetToFocus(LobbyExitConfirmation->TakeWidget());
	ModeUI.SetHideCursorDuringCapture(false);
	ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(ModeUI);
	LobbyExitConfirmation->FocusInitialChoice();
}

void UBoarUIFlowComponent::RemoveLobbyExitConfirmation()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!LobbyExitConfirmation)
		return;
	LobbyExitConfirmation->OnDecision.RemoveDynamic(this, &UBoarUIFlowComponent::ResolveLobbyExit);
	LobbyExitConfirmation->RemoveFromParent();
	LobbyExitConfirmation = nullptr;
}

void UBoarUIFlowComponent::ResolveLobbyExit(bool bConfirmed)
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!LobbyExitConfirmation || !PauseWidget || PC->bLevelTransitionRequested)
		return;
	RemoveLobbyExitConfirmation();
	if (!bConfirmed || (PC->LobbyLevel.IsNull() && PC->LobbyLevelName.IsNone()))
	{
		PauseWidget->SetVisibility(ESlateVisibility::Visible);
		FInputModeGameAndUI ModeUI;
		ModeUI.SetWidgetToFocus(PauseWidget->TakeWidget());
		ModeUI.SetHideCursorDuringCapture(false);
		ModeUI.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(ModeUI);
		PauseWidget->FocusLobbyChoice();
		return;
	}
	ResumeFromPause();
	// 途中退出は既存のLobby遷移だけを呼び、Clear結果を保存しません。
	PC->ReturnToLobby();
}

void UBoarUIFlowComponent::OpenFacilityMenu(bool bArchive)
{
	auto *PC = GetController();
	if (!PC)
		return;
	const auto *Facility = GetWorld()->GetAuthGameMode<ABoarFacilityGameMode>();
	if (!PC->IsLocalController() || !Facility || Facility->bGadgetTest || Facility->bArchive != bArchive || bFacilityMenuOpen ||
		PauseWidget || SettingsWidget || LoadoutWidget || EncyclopediaWidget || PC->bLevelTransitionRequested)
		return;
	UUserWidget *Menu = nullptr;
	if (bArchive)
	{
		if (!PC->EncyclopediaWidgetClass)
			return;
		EncyclopediaWidget = CreateWidget<UBoarEncyclopediaWidget>(PC, PC->EncyclopediaWidgetClass);
		if (!EncyclopediaWidget)
			return;
		EncyclopediaWidget->OnClosed.AddUniqueDynamic(this, &UBoarUIFlowComponent::CloseEncyclopedia);
		Menu = EncyclopediaWidget;
	}
	else
	{
		if (!PC->LoadoutWidgetClass)
			return;
		LoadoutWidget = CreateWidget<UBoarLoadoutWidget>(PC, PC->LoadoutWidgetClass);
		if (!LoadoutWidget)
			return;
		LoadoutWidget->SetLabContext(true);
		LoadoutWidget->OnClosed.AddUniqueDynamic(this, &UBoarUIFlowComponent::CloseLoadoutMenu);
		Menu = LoadoutWidget;
	}
	bFacilityMenuOpen = true;
	bFacilityAddedUIContext = PC->UIMappingContext && !PC->OwnedMappingContexts.Contains(PC->UIMappingContext);
	if (bFacilityAddedUIContext)
		PC->AddOwnedMappingContext(PC->UIMappingContext, 20);
	PC->bIsGadgetModifierHeld = false;
	if (auto *FacilityPlayer = PC->GetBoarCharacter())
	{
		FacilityPlayer->StopDash();
		FacilityPlayer->StopJump();
		FacilityPlayer->StopGadgetUse();
		FacilityPlayer->SetMenuOpen(true);
		FacilityPlayer->GetCharacterMovement()->StopMovementImmediately();
	}
	PC->FlushPressedKeys();
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);
	Menu->AddToViewport(100);
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(Menu->TakeWidget());
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	if (bArchive)
		EncyclopediaWidget->FocusInitialChoice();
	else
		LoadoutWidget->FocusInitialChoice();
}

void UBoarUIFlowComponent::OpenArchiveDisplaySelection()
{
	auto *PC = GetController();
	if (!PC)
		return;
	const auto *Mode = GetWorld()->GetAuthGameMode<ABoarFacilityGameMode>();
	if (!PC->IsLocalController() || !Mode || !Mode->bArchive || !PC->CanProcessGameplayInput() || !PC->ArchiveSelectionWidgetClass)
		return;
	ArchiveSelectionWidget = CreateWidget<UBoarArchiveSelectionWidget>(PC, PC->ArchiveSelectionWidgetClass);
	if (!ArchiveSelectionWidget)
		return;
	ArchiveSelectionWidget->TakeWidget();
	if (!ArchiveSelectionWidget->IsReadyForDisplay())
	{
		UE_LOG(LogTemp, Error, TEXT("[Archive] Display Selection UI could not build its layout."));
		ArchiveSelectionWidget = nullptr;
		return;
	}
	ArchiveSelectionWidget->OnClosed.AddUniqueDynamic(this, &UBoarUIFlowComponent::CloseArchiveDisplaySelection);
	bFacilityMenuOpen = true;
	bFacilityAddedUIContext = PC->UIMappingContext && !PC->OwnedMappingContexts.Contains(PC->UIMappingContext);
	if (bFacilityAddedUIContext)
		PC->AddOwnedMappingContext(PC->UIMappingContext, 20);
	PC->bIsGadgetModifierHeld = false;
	if (auto *ArchivePlayer = PC->GetBoarCharacter())
	{
		ArchivePlayer->StopDash();
		ArchivePlayer->StopJump();
		ArchivePlayer->StopGadgetUse();
		ArchivePlayer->SetMenuOpen(true);
		ArchivePlayer->GetCharacterMovement()->StopMovementImmediately();
	}
	PC->FlushPressedKeys();
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);
	ArchiveSelectionWidget->AddToViewport(100);
	FInputModeUIOnly Input;
	Input.SetWidgetToFocus(ArchiveSelectionWidget->TakeWidget());
	Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Input);
	PC->SetShowMouseCursor(true);
	ArchiveSelectionWidget->FocusInitialChoice();
}

void UBoarUIFlowComponent::CloseArchiveDisplaySelection()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!ArchiveSelectionWidget)
		return;
	ArchiveSelectionWidget->OnClosed.RemoveDynamic(this, &UBoarUIFlowComponent::CloseArchiveDisplaySelection);
	ArchiveSelectionWidget->RemoveFromParent();
	ArchiveSelectionWidget = nullptr;
	CloseFacilityMenuInput();
}

void UBoarUIFlowComponent::CloseFacilityMenuInput()
{
	auto *PC = GetController();
	if (!PC)
		return;
	if (!bFacilityMenuOpen)
		return;
	bFacilityMenuOpen = false;
	if (bFacilityAddedUIContext)
		PC->RemoveOwnedMappingContext(PC->UIMappingContext);
	bFacilityAddedUIContext = false;
	if (auto *FacilityPlayer = PC->GetBoarCharacter())
		FacilityPlayer->SetMenuOpen(false);
	PC->SetIgnoreMoveInput(false);
	PC->SetIgnoreLookInput(false);
	PC->FlushPressedKeys();
	FInputModeGameOnly Mode;
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(false);
}
