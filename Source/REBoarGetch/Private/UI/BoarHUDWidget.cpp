#include "UI/BoarHUDWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Gadget/GadgetBase.h"
#include "Component/GadgetComponent.h"
#include "Gadget/GadgetDataAsset.h"

namespace
{
	UTextBlock* ResolveHUDTextBlock(UWidgetTree* WidgetTree, const FName WidgetName)
	{
		if (!WidgetTree || WidgetName.IsNone())
		{
			return nullptr;
		}

		return Cast<UTextBlock>(WidgetTree->FindWidget(WidgetName));
	}
}

void UBoarHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ResolveWidgetReferences();
}

// 未配置のWidgetも含めてスロット順を維持し、装備添字と表示の対応を揃えます。
void UBoarHUDWidget::ResolveWidgetReferences()
{
	CapturedCountText = ResolveHUDTextBlock(WidgetTree, CapturedCountTextWidgetName);
	TargetCountText = ResolveHUDTextBlock(WidgetTree, TargetCountTextWidgetName);
	CurrentHealthText = ResolveHUDTextBlock(WidgetTree, CurrentHealthTextWidgetName);
	MaxHealthText = ResolveHUDTextBlock(WidgetTree, MaxHealthTextWidgetName);
	StaminaBarWidget = WidgetTree && !StaminaBarWidgetName.IsNone()
		             ? Cast<UProgressBar>(WidgetTree->FindWidget(StaminaBarWidgetName))
		             : nullptr;
	if (StaminaBarWidget)
	{
		// 最初の通知と位置計算が済むまで、画面左上などに一瞬表示されないようにする。
		StaminaBarWidget->SetRenderOpacity(0.0f);
	}

	GadgetIconWidgets.Reset();
	GadgetSelectionWidgets.Reset();
	for (int32 SlotIndex = 0; SlotIndex < UGadgetComponent::GetGadgetSlotCount(); ++SlotIndex)
	{
		const FName IconName = GadgetIconWidgetNames.IsValidIndex(SlotIndex)
			                       ? GadgetIconWidgetNames[SlotIndex]
			                       : NAME_None;
		const FName SelectionName = GadgetSelectionWidgetNames.IsValidIndex(SlotIndex)
			                            ? GadgetSelectionWidgetNames[SlotIndex]
			                            : NAME_None;
		GadgetIconWidgets.Add(WidgetTree ? Cast<UImage>(WidgetTree->FindWidget(IconName)) : nullptr);
		GadgetSelectionWidgets.Add(WidgetTree ? WidgetTree->FindWidget(SelectionName) : nullptr);
	}
}

// 装備Actorを生成せず、クラス既定値の定義からアイコンを取得して選択枠とともに描画します。
void UBoarHUDWidget::UpdateGadgetSlots(
	const TArray<TSubclassOf<AGadgetBase>>& GadgetSlots,
	int32 SelectedSlotIndex)
{
	for (int32 SlotIndex = 0; SlotIndex < UGadgetComponent::GetGadgetSlotCount(); ++SlotIndex)
	{
		UTexture2D* DisplayIcon = nullptr;
		if (GadgetSlots.IsValidIndex(SlotIndex) && GadgetSlots[SlotIndex])
		{
			const AGadgetBase* GadgetDefaults = GadgetSlots[SlotIndex]->GetDefaultObject<AGadgetBase>();
			const UGadgetDataAsset* Definition = GadgetDefaults ? GadgetDefaults->GetGadgetDefinition() : nullptr;
			DisplayIcon = Definition ? Definition->DisplayIcon : nullptr;
		}

		if (GadgetIconWidgets.IsValidIndex(SlotIndex) && GadgetIconWidgets[SlotIndex])
		{
			GadgetIconWidgets[SlotIndex]->SetBrushFromTexture(DisplayIcon, true);
			GadgetIconWidgets[SlotIndex]->SetVisibility(
				DisplayIcon ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}

		if (GadgetSelectionWidgets.IsValidIndex(SlotIndex) && GadgetSelectionWidgets[SlotIndex])
		{
			GadgetSelectionWidgets[SlotIndex]->SetVisibility(
				SlotIndex == SelectedSlotIndex
					? ESlateVisibility::SelfHitTestInvisible
					: ESlateVisibility::Hidden);
		}
	}
}

void UBoarHUDWidget::UpdateCaptureCount(int32 CurrentCount, int32 TargetCount)
{
	if (CapturedCountText)
	{
		CapturedCountText->SetText(FText::AsNumber(CurrentCount));
	}

	if (TargetCountText)
	{
		TargetCountText->SetText(FText::AsNumber(TargetCount));
	}
}

// 受け取ったHPを整数表示へ丸めます。ダメージ計算やHPの更新はゲームプレイ側が担当します。
void UBoarHUDWidget::UpdateHealth(float CurrentHealth, float MaxHealth)
{
	if (UProgressBar* HealthBar = Cast<UProgressBar>(GetWidgetFromName(TEXT("HealthMeter"))))
	{
		HealthBar->SetPercent(MaxHealth > 0.f ? FMath::Clamp(CurrentHealth / MaxHealth, 0.f, 1.f) : 0.f);
	}

	if (CurrentHealthText)
	{
		CurrentHealthText->SetText(FText::AsNumber(FMath::RoundToInt(CurrentHealth)));
	}

	if (MaxHealthText)
	{
		MaxHealthText->SetText(FText::AsNumber(FMath::RoundToInt(MaxHealth)));
	}
}

// 割合と色だけを決めます。消費・回復の計算や禁止判定はCharacterが担当します。
void UBoarHUDWidget::UpdateStamina(float CurrentStamina, float MaxStamina, bool bDashLocked)
{
	const float Ratio = MaxStamina > 0.0f ? FMath::Clamp(CurrentStamina / MaxStamina, 0.0f, 1.0f) : 0.0f;
	const bool bFull = Ratio >= 1.0f;
	if (!bFull)
	{
		// 減り始めた・回復中の間は表示を続け、満タンに戻ってから非表示までの待ち時間を数え直す。
		StaminaFullElapsed = 0.0f;
	}
	bStaminaFull = bFull;
	bStaminaLocked = bDashLocked;

	if (StaminaBarWidget)
	{
		StaminaBarWidget->SetPercent(Ratio);
		StaminaBarWidget->SetFillColorAndOpacity(bDashLocked
			                                         ? StaminaExhaustedColor
			                                         : (Ratio < StaminaLowRatio ? StaminaLowColor : StaminaNormalColor));
	}
}

void UBoarHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!StaminaBarWidget)
		return;

	if (bStaminaFull)
	{
		StaminaFullElapsed += InDeltaTime;
	}
	// 満タンで一定時間たったら隠し、Dashで減り始めたらすぐ表示する。
	const bool bWantVisible = !bStaminaFull || StaminaHideDelayWhenFull < 0.0f ||
		StaminaFullElapsed < StaminaHideDelayWhenFull;
	// プレイヤーが画面外・カメラの後ろにいる場合は表示しない。
	const bool bOnScreen = UpdateStaminaGaugePosition();

	const float TargetOpacity = (bWantVisible && bOnScreen) ? 1.0f : 0.0f;
	StaminaGaugeOpacity = FMath::FInterpConstantTo(StaminaGaugeOpacity, TargetOpacity, InDeltaTime, StaminaFadeSpeed);
	if (!bOnScreen)
	{
		StaminaGaugeOpacity = 0.0f;
	}

	// スタミナ切れ（再使用しきい値まで回復していない）間は点滅させ、Dashできないことを示す。
	float Blink = 1.0f;
	if (bStaminaLocked && StaminaExhaustedBlinkRate > 0.0f)
	{
		StaminaBlinkTime += InDeltaTime;
		Blink = 0.55f + 0.45f * (0.5f + 0.5f * FMath::Cos(StaminaBlinkTime * StaminaExhaustedBlinkRate * UE_TWO_PI));
	}
	else
	{
		StaminaBlinkTime = 0.0f;
	}
	StaminaBarWidget->SetRenderOpacity(StaminaGaugeOpacity * Blink);
}

bool UBoarHUDWidget::UpdateStaminaGaugePosition()
{
	APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(StaminaBarWidget->Slot);
	if (!Pawn || !CanvasSlot)
		return false;

	// カメラの右・上方向にずらしたワールド位置を投影し、キャラクターのすぐ横に置く。
	const FRotator CameraRotation = PC->PlayerCameraManager
		                                ? PC->PlayerCameraManager->GetCameraRotation()
		                                : PC->GetControlRotation();
	const FRotationMatrix CameraAxes(CameraRotation);
	const FVector WorldLocation = Pawn->GetActorLocation() +
		CameraAxes.GetUnitAxis(EAxis::Y) * StaminaGaugeWorldOffset.X +
		FVector::UpVector * StaminaGaugeWorldOffset.Y;

	FVector2D WidgetPosition;
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, WorldLocation, WidgetPosition, true))
		return false;

	CanvasSlot->SetPosition(WidgetPosition);
	return true;
}
