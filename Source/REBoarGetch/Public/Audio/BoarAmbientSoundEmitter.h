#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoarAmbientSoundEmitter.generated.h"

class UBoarLoopingSoundComponent;
class UBillboardComponent;

/**
 * Levelに置く環境音の発音位置です（水辺・風車・森・尾根の風など）。Gameplay機能・Collisionは持ちません。
 * エンジン標準のAmbient Soundと違い、BoarAudioManagerSubsystem経由で鳴らすため設定画面のSE音量に従います。
 * 音源はSoundComponentのAmbienceType（Project Settingsの共通音源）または個別のSoundで設定します。
 */
UCLASS(meta = (DisplayName = "Boar Ambient Sound Emitter"))
class REBOARGETCH_API ABoarAmbientSoundEmitter : public AActor
{
	GENERATED_BODY()

public:
	ABoarAmbientSoundEmitter();

	UFUNCTION(BlueprintPure, Category = "Audio")
	UBoarLoopingSoundComponent* GetSoundComponent() const { return SoundComponent; }

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoarLoopingSoundComponent> SoundComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif
};
