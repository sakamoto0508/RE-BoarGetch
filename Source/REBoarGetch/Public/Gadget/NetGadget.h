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
	
	/** ネットを使用します。 */
	virtual void Use_Implementation(AActor* TargetActor) override;

	/** 捕獲判定を有効化します。AnimNotify から呼び出します。 */
	UFUNCTION(BlueprintCallable, Category = "Gadget|Net")
	void BeginCaptureWindow();

	/** 捕獲判定を無効化します。AnimNotify から呼び出します。 */
	UFUNCTION(BlueprintCallable, Category = "Gadget|Net")
	void EndCaptureWindow();

private:
	UFUNCTION()
	void OnCaptureCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	bool TryCaptureBoar(ABoarBase* Boar);

	void CaptureOverlappingBoars();

	/** ネットの捕獲範囲です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	float CaptureRadius = 300.0f;

	/** 捕獲判定用のコリジョンです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> CaptureCollision;

	/** 捕獲判定用のコリジョンが有効かどうかです。AnimNotify で制御します。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	bool bCaptureWindowActive = false;
};
