#include "Stage/BoarLevelTransitionLibrary.h"
#include "ACFTransitionCatalog.h"
#include "ACFTransitionPreset.h"
#include "ACFTransitionSubsystem.h"
#include "Kismet/GameplayStatics.h"

UBoarLevelTransitionSettings::UBoarLevelTransitionSettings()
{
	// iniに値が無いときの既定。変更はProject Settings > Game > Boar Level Transitionsで行う。
	Portal.Effect = EACFTransitionEffect::DigitalGlitch;          // 12
	Menu.Effect = EACFTransitionEffect::CRTPowerOff;              // 32
	StageDeparture.Effect = EACFTransitionEffect::WireframeConversion; // 30
	ReturnToLobby.Effect = EACFTransitionEffect::HologramTransfer;     // 29
}

const FBoarLevelTransitionEntry& UBoarLevelTransitionSettings::GetEntry(EBoarLevelTransitionStyle Style) const
{
	switch (Style)
	{
	case EBoarLevelTransitionStyle::Portal: return Portal;
	case EBoarLevelTransitionStyle::StageDeparture: return StageDeparture;
	case EBoarLevelTransitionStyle::ReturnToLobby: return ReturnToLobby;
	default: return Menu;
	}
}

EACFTransitionEffect UBoarLevelTransitionLibrary::GetTransitionEffect(EBoarLevelTransitionStyle Style)
{
	return GetDefault<UBoarLevelTransitionSettings>()->GetEntry(Style).Effect;
}

void UBoarLevelTransitionLibrary::OpenLevelWithTransition(const UObject* WorldContextObject, FName LevelName, EBoarLevelTransitionStyle Style,
                                                          bool bAbsolute, FString Options)
{
	OpenLevelWithTransitionEntry(WorldContextObject, LevelName, GetDefault<UBoarLevelTransitionSettings>()->GetEntry(Style), bAbsolute, Options);
}

void UBoarLevelTransitionLibrary::OpenLevelWithTransitionEntry(const UObject* WorldContextObject, FName LevelName, const FBoarLevelTransitionEntry& Entry,
                                                               bool bAbsolute, FString Options)
{
	if (LevelName.IsNone())
		return;
	// ACFは常にbAbsolute=trueでOpenLevelするため、相対Travelの要求だけは演出なしで直接開く。
	UACFTransitionSubsystem* Subsystem = bAbsolute ? UACFTransitionSubsystem::Get(WorldContextObject) : nullptr;
	if (Subsystem)
	{
		FACFTransitionOptions TransitionOptions;

		// 覆っている間の操作で二重遷移や移動が起きないよう、入力を止める（終了・失敗時はACFが戻す）。
		TransitionOptions.bLockInput = true;
		FACFTransitionHandle Handle;

		if (const UACFTransitionPreset* Preset = Entry.Preset.LoadSynchronous())
		{
			Handle = Subsystem->PlayLevelTransitionPreset(Preset, TransitionOptions, LevelName, Options);
		}
		else
		{
			if (!Entry.Preset.IsNull())
				UE_LOG(LogTemp, Warning, TEXT("[LevelTransition] Preset %s を読み込めないため、Effect %d を使います。"),
					*Entry.Preset.ToString(), static_cast<int32>(Entry.Effect));
			Handle = Subsystem->PlayLevelTransition(
				ACFTransition::MakeDefaultSettings(Entry.Effect, EACFTransitionPhase::Close),
				ACFTransition::MakeDefaultSettings(Entry.Effect, EACFTransitionPhase::Open),
				TransitionOptions, LevelName, Options);
		}

		if (Handle.IsValid())
			return;

		UE_LOG(LogTemp, Warning, TEXT("[LevelTransition] ACFが %s への遷移を拒否したため、演出なしで開きます。"), *LevelName.ToString());
	}

	UGameplayStatics::OpenLevel(WorldContextObject, LevelName, bAbsolute, Options);
}

void UBoarLevelTransitionLibrary::OpenLevelBySoftObjectPtrWithTransition(const UObject* WorldContextObject, const TSoftObjectPtr<UWorld> Level,
                                                                         EBoarLevelTransitionStyle Style, bool bAbsolute, FString Options)
{
	// UGameplayStatics::OpenLevelBySoftObjectPtrと同じく、Package名へ変換して開く。
	const FName LevelName = FName(*FPackageName::ObjectPathToPackageName(Level.ToString()));
	OpenLevelWithTransition(WorldContextObject, LevelName, Style, bAbsolute, Options);
}
