#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BoarPlayerCharacter.generated.h"

class UHealthComponent;
class UGadgetComponent;

/// <summary>
/// プレイヤーキャラクターの土台クラスです。
/// Unity でいうと、入力を受ける本体をここに置き、
/// HP やガチャメカの処理は Component に分ける設計です。
/// </summary>
UCLASS()
class ABoarPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/// <summary>
	/// コンストラクタです。
	/// コンポーネントの生成と、移動系の初期設定を行います。
	/// </summary>
	ABoarPlayerCharacter();

	/// <summary>
	/// 入力のバインドを行います。
	/// Move / Look / Jump をここにまとめます。
	/// </summary>
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/// <summary>
	/// HP 管理コンポーネントを返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category="Player")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	/// <summary>
	/// ガチャメカ管理コンポーネントを返します。
	/// </summary>
	UFUNCTION(BlueprintPure, Category="Player")
	UGadgetComponent* GetGadgetComponent() const { return GadgetComponent; }

protected:
	/// <summary>
	/// ゲーム開始時の初期化を行います。
	/// </summary>
	virtual void BeginPlay() override;

private:
	/// <summary>
	/// HP を管理するコンポーネントです。
	/// </summary>
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UHealthComponent> HealthComponent;

	/// <summary>
	/// ガチャメカを管理するコンポーネントです。
	/// </summary>
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UGadgetComponent> GadgetComponent;

	/// <summary>
	/// 回転速度です。
	/// </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(AllowPrivateAccess="true"))
	float RotationRateYaw = 720.0f;

	/// <summary>
	/// ジャンプの強さです。
	/// </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(AllowPrivateAccess="true"))
	float JumpZVelocity = 700.0f;

	/// <summary>
	/// 空中操作のしやすさです。
	/// </summary>
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(AllowPrivateAccess="true", ClampMin="0.0", ClampMax="1.0"))
	float AirControl = 0.35f;

	/// <summary>
	/// 前後移動を処理します。
	/// </summary>
	void MoveForward(float Value);

	/// <summary>
	/// 左右移動を処理します。
	/// </summary>
	void MoveRight(float Value);

	/// <summary>
	/// 視点の左右回転を処理します。
	/// </summary>
	void Turn(float Value);

	/// <summary>
	/// 視点の上下回転を処理します。
	/// </summary>
	void LookUp(float Value);

	/// <summary>
	/// ジャンプ開始を処理します。
	/// </summary>
	void StartJump();

	/// <summary>
	/// ジャンプ終了を処理します。
	/// </summary>
	void StopJump();
};