#include "ACFCameraSettings.h"
#include "ACFCameraOutputModifier.h"

UACFCameraSettings::UACFCameraSettings()
{
	OutputModifierClass = UACFCameraOutputModifier::StaticClass();
}

TSubclassOf<UACFCameraOutputModifier> UACFCameraSettings::ResolveOutputModifierClass() const
{
	// Loaded at most once per attach (first request / controller change), never per frame.
	if (UClass* Loaded = OutputModifierClass.LoadSynchronous())
	{
		return Loaded;
	}
	return UACFCameraOutputModifier::StaticClass();
}
