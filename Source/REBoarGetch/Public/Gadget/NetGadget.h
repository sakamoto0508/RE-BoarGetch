// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GadgetBase.h"
#include "NetGadget.generated.h"

class ABoarBase;
class USphereComponent;
class UPrimitiveComponent;
struct FHitResult;

UCLASS()
class REBOARGETCH_API ANetGadget : public AGadgetBase
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ANetGadget();
	
	/// <summary> ネットを使用します。 </summary>
	virtual void Use_Implementation(AActor* TargetActor) override;

	/// <summary> 捕獲判定を有効化します。AnimNotify から呼び出します。 </summary>
	UFUNCTION(BlueprintCallable, Category = "Gadget|Net")
	void BeginCaptureWindow();

	/// <summary> 捕獲判定を無効化します。AnimNotify から呼び出します。 </summary>
	UFUNCTION(BlueprintCallable, Category = "Gadget|Net")
	void EndCaptureWindow();

private:
	UFUNCTION()
	void OnCaptureCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	bool TryCaptureBoar(ABoarBase* Boar);

	void CaptureOverlappingBoars();

	/// <summary> ネットの捕獲範囲です。</summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true", ClampMin = "100.0"))
	float CaptureRadius = 300.0f;

	/// <summary> 捕獲判定用のコリジョンです。</summary>
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> CaptureCollision;

	/// <sumarry> 捕獲判定用のコリジョンが有効かどうかです。AnimNotify で制御します。</sumarry>					
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	bool bCaptureWindowActive = false;
};
