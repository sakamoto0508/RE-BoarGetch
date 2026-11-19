#include "Audio/BoarAmbientSoundEmitter.h"

#include "Audio/BoarLoopingSoundComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

ABoarAmbientSoundEmitter::ABoarAmbientSoundEmitter()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	SoundComponent = CreateDefaultSubobject<UBoarLoopingSoundComponent>(TEXT("SoundComponent"));
	RootComponent = SoundComponent;

#if WITH_EDITORONLY_DATA
	// Editorで選択・移動できるよう、Ambient Soundと同じアイコンを表示する（ゲームでは表示しない）。
	Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		static ConstructorHelpers::FObjectFinder<UTexture2D> Icon(TEXT("/Engine/EditorResources/AudioIcons/S_Ambient_Sound_Simple"));
		if (Icon.Succeeded())
			Sprite->SetSprite(Icon.Object);
		Sprite->SetupAttachment(SoundComponent);
		Sprite->bIsScreenSizeScaled = true;
	}
#endif
}
