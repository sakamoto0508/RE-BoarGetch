#include "Boar/BoarDataAsset.h"

namespace
{
	FBoarArchetypeSettings MakeSettings(
		float PlayerWeight,
		float CageWeight,
		float EscapeWeight,
		bool bCanAttackCage,
		bool bUseStamina)
	{
		FBoarArchetypeSettings Settings;
		Settings.PlayerPriorityWeight = PlayerWeight;
		Settings.CagePriorityWeight = CageWeight;
		Settings.EscapePriorityWeight = EscapeWeight;
		Settings.bCanAttackCage = bCanAttackCage;
		Settings.bUseStamina = bUseStamina;
		return Settings;
	}

	const TMap<EBoarArchetype, FBoarArchetypeSettings>& GetBuiltInSettings()
	{
		static const TMap<EBoarArchetype, FBoarArchetypeSettings> Settings = []
		{
			TMap<EBoarArchetype, FBoarArchetypeSettings> Result;
			Result.Add(EBoarArchetype::Normal, MakeSettings(0.8f, 0.9f, 1.4f, true, false));
			Result.Add(EBoarArchetype::PlayerAttacker, MakeSettings(1.8f, 0.8f, 0.4f, true, false));

			FBoarArchetypeSettings EscapeSettings = MakeSettings(0.4f, 0.1f, 2.2f, false, true);
			EscapeSettings.StaminaDrainPerSecond = 30.0f;
			EscapeSettings.StaminaRecoveryPerSecond = 35.0f;
			Result.Add(EBoarArchetype::EscapeSpecialist, EscapeSettings);

			Result.Add(EBoarArchetype::CageBreaker, MakeSettings(0.7f, 2.2f, 0.5f, true, false));
			return Result;
		}();
		return Settings;
	}
}

UBoarDataAsset::UBoarDataAsset()
	: ArchetypeSettings(GetBuiltInSettings())
{
}

const FBoarArchetypeSettings& UBoarDataAsset::GetSettings(EBoarArchetype Archetype) const
{
	if (const FBoarArchetypeSettings* Settings = ArchetypeSettings.Find(Archetype))
	{
		return *Settings;
	}

	return GetDefaultSettings(Archetype);
}

const FBoarArchetypeSettings& UBoarDataAsset::GetDefaultSettings(EBoarArchetype Archetype)
{
	const TMap<EBoarArchetype, FBoarArchetypeSettings>& Settings = GetBuiltInSettings();
	if (const FBoarArchetypeSettings* FoundSettings = Settings.Find(Archetype))
	{
		return *FoundSettings;
	}

	return Settings.FindChecked(EBoarArchetype::Normal);
}
