// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GadgetBase.generated.h"

UENUM(BlueprintType)
enum class EGadgetUseStyle : uint8
{
	OneShot,
	Hold
};

/// <summary>
/// ガチャメカの基底クラスです。
/// Net / Radar / Jet などはここから派生します。
/// </summary>
UCLASS()
class REBOARGETCH_API AGadgetBase : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AGadgetBase();

	/// <summary>
	/// ガチャメカを使用します。
	/// 派生クラスで実際の効果を実装します。
	/// 一時的な参照なのでポインタ。
	/// </summary>
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Gadget")
	void Use(AActor* TargetActor);

	/// <summary>
	/// C++ 側のデフォルト挙動です。
	/// 一時的な参照なのでポインタ。
	/// </summary>
	virtual void Use_Implementation(AActor* TargetActor);

	/// <summary>
	/// 使用可能かどうかを返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category="Gadget")
	bool CanUse() const { return !bIsOnCooldown; }

	/// <summary>
	/// 現在使用中かどうかを返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category = "Gadget")
	bool IsUsing() const { return bIsUsing; }

	/// <summary>
	/// ガジェットの使用タイプ（単発/継続）を返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category = "Gadget")
	EGadgetUseStyle GetUseStyle() const { return UseStyle; }

	/// <summary>
	/// 使用開始時に呼ばれる入口です。
	/// Character側はこの関数を呼んで使用ライフサイクルを開始します。
	/// </summary>
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Gadget")
	void BeginUse(AActor* TargetActor);
	virtual void BeginUse_Implementation(AActor* TargetActor);

	/// <summary>
	/// 使用終了時に呼ばれる入口です。
	/// Hold系ガジェットの停止処理に使います。
	/// </summary>
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Gadget")
	void EndUse(AActor* TargetActor);
	virtual void EndUse_Implementation(AActor* TargetActor);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	/// <summary>
	/// 使用タイプです。
	/// OneShotは押下時に完結、Holdは押している間継続します。
	/// </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	EGadgetUseStyle UseStyle = EGadgetUseStyle::OneShot;

	/// <summary>
	/// クールタイムです。
	/// </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float CooldownSeconds = 1.0f;

	/// <summary>
	/// クールタイム中かどうかです。
	/// </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	bool bIsOnCooldown = false;

	/// <summary>
	/// 使用中かどうかです。
	/// </summary>
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	bool bIsUsing = false;

	/// <summary>
	/// クールタイム解除用のタイマーです。
	/// </summary>
	FTimerHandle CooldownTimerHandle = FTimerHandle(); //明示的初期化。

	/// <summary>
	/// クールタイムを開始します。
	/// </summary>
	void StartCooldown();

	/// <summary>
	/// クールタイム終了処理です。
	/// </summary>
	void FinishCooldown();

	/// <summary>
	/// 攻撃ヒット対象に起動通知を送ります（ダメージ系ガジェット用）。
	/// </summary>
	void TryActivateByAttack(AActor* HitActor, AActor* AttackInstigator);
};
