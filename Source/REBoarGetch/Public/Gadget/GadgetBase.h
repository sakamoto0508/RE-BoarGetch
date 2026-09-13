// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GadgetBase.generated.h"

class UGadgetDataAsset;

/** 入力に対してガジェット効果を単発実行するか、押下中継続するかを表します。 */
UENUM(BlueprintType)
enum class EGadgetUseStyle : uint8
{
	/** 押した瞬間に効果が完結する使用形式です。 */
	OneShot,
	/** 押している間だけ効果を継続し、入力終了時に停止する使用形式です。 */
	Hold
};

/**
 * ガチャメカの基底クラスです。
 * Net / Radar / Jet などはここから派生します。
 */
UCLASS()
class REBOARGETCH_API AGadgetBase : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	/** Tickを使用しないガジェットActorの初期状態を構築します。 */
	AGadgetBase();

	/**
	 * ガチャメカを使用します。
	 * 派生クラスで実際の効果を実装します。
	 * 一時的な参照なのでポインタ。
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Gadget")
	void Use(AActor* TargetActor);

	/**
	 * C++ 側のデフォルト挙動です。
	 * 一時的な参照なのでポインタ。
	 */
	virtual void Use_Implementation(AActor* TargetActor);

	/**
	 * 使用中に移動可能かどうかを返します。
	 */
	UFUNCTION(BlueprintPure, Category="Gadget")
	bool CanMoveWhileUsing() const { return bCanMoveWhileUsing; }

	/**
	 * 使用可能かどうかを返します。
	 */
	UFUNCTION(BlueprintPure, Category="Gadget")
	bool CanUse() const { return !bIsOnCooldown; }

	/**
	 * 現在使用中かどうかを返します。
	 */
	UFUNCTION(BlueprintPure, Category = "Gadget")
	bool IsUsing() const { return bIsUsing; }

	/**
	 * ガジェットの使用タイプ（単発/継続）を返します。
	 */
	UFUNCTION(BlueprintPure, Category = "Gadget")
	EGadgetUseStyle GetUseStyle() const { return UseStyle; }

	/**
	 * 装備時に接続するSocket名を返します。
	 */
	UFUNCTION(BlueprintPure, Category = "Gadget|Equip")
	FName GetEquipSocketName() const { return EquipSocketName; }

	/** UIを含む各機能が共有するガジェット定義を返します。 */
	UFUNCTION(BlueprintPure, Category = "Gadget")
	const UGadgetDataAsset* GetGadgetDefinition() const { return GadgetDefinition; }

	/**
	 * 使用開始時に呼ばれる入口です。
	 * Character側はこの関数を呼んで使用ライフサイクルを開始します。
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Gadget")
	void BeginUse(AActor* TargetActor);
	/** Blueprintで上書きされない場合の使用開始処理です。 */
	virtual void BeginUse_Implementation(AActor* TargetActor);

	/**
	 * 使用終了時に呼ばれる入口です。
	 * Hold系ガジェットの停止処理に使います。
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Gadget")
	void EndUse(AActor* TargetActor);
	/** Blueprintで上書きされない場合の使用終了処理です。 */
	virtual void EndUse_Implementation(AActor* TargetActor);

protected:
	/** 表示情報などを一元管理するガジェット定義です。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget")
	TObjectPtr<UGadgetDataAsset> GadgetDefinition;

	/**
	 * 装備時に接続するプレイヤーMeshのSocket名です。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget|Equip", meta = (AllowPrivateAccess = "true"))
	FName EquipSocketName = TEXT("GadgetSocket");

	/**
	 * 使用タイプです。
	 * OneShotは押下時に完結、Holdは押している間継続します。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	EGadgetUseStyle UseStyle = EGadgetUseStyle::OneShot;

	/**
	 * クールタイムです。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float CooldownSeconds = 1.0f;

	/**
	 * 使用中に移動可能かどうかです。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gadget", meta =(AllowPrivateAccess = "true"))
	bool bCanMoveWhileUsing = false;

	/**
	 * クールタイム中かどうかです。
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	bool bIsOnCooldown = false;

	/**
	 * 使用中かどうかです。
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Gadget", meta = (AllowPrivateAccess = "true"))
	bool bIsUsing = false;

	/**
	 * クールタイム解除用のタイマーです。
	 */
	FTimerHandle CooldownTimerHandle = FTimerHandle(); //明示的初期化。

	/**
	 * クールタイムを開始します。
	 */
	void StartCooldown();

	/**
	 * クールタイム終了処理です。
	 */
	void FinishCooldown();

	/**
	 * 攻撃ヒット対象に起動通知を送ります（ダメージ系ガジェット用）。
	 */
	void TryActivateByAttack(AActor* HitActor, AActor* AttackInstigator);
};
