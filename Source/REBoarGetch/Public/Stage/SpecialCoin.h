#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpecialCoin.generated.h"

/** ステージIDとコインIDで取得状態を保存する特別コインの基底Actorです。 */
UCLASS(Blueprintable)
class REBOARGETCH_API ASpecialCoin : public AActor
{
	GENERATED_BODY()
public:
	/** 見た目や接触判定は派生BPで設定し、取得処理だけを共通化します。 */
	ASpecialCoin();
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Coin") FName SpecialCoinId;
	/** Player接触等から呼びます。保存済み/今回取得済みの重複取得は拒否します。 */
	UFUNCTION(BlueprintCallable, Category = "Coin") bool TryCollect(AActor* Collector);
protected:
	virtual void BeginPlay() override;
	UFUNCTION(BlueprintImplementableEvent, Category = "Coin") void OnCoinCollected();
private:
	bool bCollected = false;
};
