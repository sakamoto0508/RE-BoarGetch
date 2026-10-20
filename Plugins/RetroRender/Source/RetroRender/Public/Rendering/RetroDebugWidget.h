#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RetroDebugWidget.generated.h"
class ARetroCompareActor;
class URetroRenderComponent;
class UTextBlock;
class USlider;

UCLASS()
class RETRORENDER_API URetroStatusWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 void Refresh(URetroRenderComponent* Renderer);
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
private:
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
};

UCLASS()
class RETRORENDER_API URetroParameterRow : public UUserWidget
{
 GENERATED_BODY()
public:
 void Configure(URetroRenderComponent* InRenderer, int32 Index);
 void Refresh();
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeConstruct() override;
private:
 UFUNCTION() void Changed(float Value);
 UPROPERTY(Transient) TObjectPtr<URetroRenderComponent> Renderer;
 UPROPERTY(Transient) TObjectPtr<USlider> Slider;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
 int32 ParameterIndex=0;
};

UCLASS()
class RETRORENDER_API URetroDebugWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 void Configure(ARetroCompareActor* InOwner) { Harness=InOwner; }
 void Refresh();
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeConstruct() override;
 virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
 void Select(uint8 Index);
 UFUNCTION() void Off();
 UFUNCTION() void PS1();
 UFUNCTION() void N64();
 UFUNCTION() void PS2();
 UFUNCTION() void CRT();
 UFUNCTION() void ToggleCRT();
 UFUNCTION() void Reset();
 UFUNCTION() void Capture();
 UFUNCTION() void Compare();
 UFUNCTION() void Close();
 UPROPERTY(Transient) TObjectPtr<ARetroCompareActor> Harness;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
 UPROPERTY(Transient) TArray<TObjectPtr<URetroParameterRow>> Rows;
};
