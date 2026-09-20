// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GadgetBase.h"
#include "NetGadget.generated.h"

class ABoarBase;
class USphereComponent;
class UPrimitiveComponent;
struct FHitResult;

/** アニメーション中の判定窓に重なったイノシシを捕獲する網ガジェットです。 */
UCLASS()
class REBOARGETCH_API ANetGadget : public AGadgetBase
{
	GENERATED_BODY()
	/** 一振り中に処理した個体です。即解放されても同じ振りで再捕獲しません。 */
	TSet<TWeakObjectPtr<class ABoarBase>> AttemptedBoarsThisUse;

public:
	// Sets default values for this actor's properties
	/** 捕獲Collisionと網用Socketを設定します。 */
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
	/** 捕獲判定窓の開始後に侵入したActorをイノシシとして捕獲できるか判定します。 */
	UFUNCTION()
	void OnCaptureCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	/** 未捕獲のイノシシであれば捕獲処理を実行し、成功可否を返します。 */
	bool TryCaptureBoar(ABoarBase* Boar);

	/** 判定窓開始時点ですでにCollision内にいる全イノシシを捕獲対象として処理します。 */
	void CaptureOverlappingBoars();

	/** 捕獲判定用のコリジョンです。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> CaptureCollision;

	/** 捕獲判定用のコリジョンが有効かどうかです。AnimNotify で制御します。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	bool bCaptureWindowActive = false;
};
