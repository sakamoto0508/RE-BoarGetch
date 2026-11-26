#pragma once

#include "CoreMinimal.h"

// UBoarGameplayCameraComponentの本体（BoarGameplayCameraComponent.cpp）と診断機能（BoarGameplayCameraDebug.cpp）が
// 共有する値です。このモジュールの中だけで使います（Private）。

DECLARE_LOG_CATEGORY_EXTERN(LogBoarCamera, Log, All);

namespace BoarGameplayCamera
{
	/** 比較用の切替。-1 = Componentの既定値（BP設定）、0 = SpringArm、1 = ACF。Level遷移後も維持されます。 */
	inline int32 ACFOverride = -1;

	/** Damping検証値（秒）。負の値 = アセットの値。Shipping以外でだけ変更できます。 */
	inline float TestPivotDamping = -1.f;
	inline float TestRotationDamping = -1.f;
	inline float TestReturnDamping = -1.f;

	/** 検証用CVarの「アセットの値を使う」を表す値です。 */
	inline constexpr float UseAssetValue = -1.f;
}
