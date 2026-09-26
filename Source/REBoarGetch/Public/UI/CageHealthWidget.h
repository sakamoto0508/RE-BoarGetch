#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CageHealthWidget.generated.h"

/** A simple horizontal world-space health bar. All values are presentation copies. */
UCLASS()
class REBOARGETCH_API UCageHealthWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetPresentation(float InFraction, float InTrail, FLinearColor InColor, float InFlash, float InPulse);
protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
private:
	float Fraction=1, Trail=1, Flash=0, Pulse=0;
	FLinearColor Color=FLinearColor(.02f,.75f,1.f);
};
