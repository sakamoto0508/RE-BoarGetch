#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StageEntrance.generated.h"

class UBoxComponent;
class UBoarLobbyWidget;
class UPrimitiveComponent;
class USceneComponent;
class UStageConfig;

/**
 * @brief ロビー内でステージ選択UIとLevel遷移を仲介する入口Actorです。
 *
 * Triggerへローカルプレイヤーが進入すると、設定されたStageConfigをLobby Widgetへ渡して表示します。
 * Widgetの決定通知ではStageConfigのSoft World参照を開き、キャンセルまたはTrigger退出時にはUIと
 * 入力モードをゲーム状態へ戻します。Level遷移要求は一度だけ受け付けます。
 */
UCLASS(Blueprintable)
class REBOARGETCH_API AStageEntrance : public AActor
{
	GENERATED_BODY()

public:
	/** @brief Scene RootとPawn検知用Box Triggerを構築します。 */
	AStageEntrance();

protected:
	/** @brief Level終了時にWidgetと入力状態を後始末します。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** @brief ローカルプレイヤーのTrigger進入を検知し、ステージ選択UIを表示します。 */
	/** @brief 操作中プレイヤーのTrigger退出を検知し、ステージ選択UIを閉じます。 */
	UFUNCTION()
	void HandleTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleTriggerEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

private:
	/** @brief 対象StageConfigを渡してLobby Widgetを表示し、UI入力へ切り替えます。 */
	void ShowStageSelection(class APlayerController* PlayerController);
	/** @brief Lobby Widgetを破棄し、遷移中でなければゲーム入力へ戻します。 */
	void CloseStageSelection();

	/** @brief Widgetの決定通知を検証し、設定されたLevelへの遷移を一度だけ開始します。 */
	UFUNCTION()
	void HandleStageStartRequested(UStageConfig* RequestedStageConfig);

	/** @brief Widgetのキャンセル通知を受けてステージ選択UIを閉じます。 */
	UFUNCTION()
	void HandleStageSelectionClosed();

	/** Actor階層の基点となるScene Componentです。 */
	UPROPERTY(VisibleAnywhere, Category = "REBoarGetch|Stage")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Pawnの進入・退出を検知するOverlap専用Boxです。 */
	UPROPERTY(VisibleAnywhere, Category = "REBoarGetch|Stage")
	TObjectPtr<UBoxComponent> Trigger;

	/** UI表示内容、クリア条件、遷移先Levelを持つステージ設定です。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "REBoarGetch|Stage", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStageConfig> StageConfig;

	/** ステージ選択画面として生成するUBoarLobbyWidget派生クラスです。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "REBoarGetch|UI", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UBoarLobbyWidget> LobbyWidgetClass;

	/** 現在Viewportへ表示しているステージ選択Widgetです。 */
	UPROPERTY(Transient)
	TObjectPtr<UBoarLobbyWidget> LobbyWidget;

	/** 現在StageEntranceを操作しているローカルPlayerControllerです。 */
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> InteractingPlayerController;

	/** 決定入力の連打による多重Level遷移を防ぐフラグです。 */
	bool bTravelRequested = false;
};
