#pragma once

#include "CoreMinimal.h"

namespace BoarPresentationTiming
{
	/**
	 * 演出・画面アニメーションを1フレームで進める時間の上限（秒）。
	 * ヒッチやロード直後の長いフレームで、演出が一気に飛ばないようにします（10fps相当より長いフレームは0.1秒として扱う）。
	 */
	constexpr float MaxFrameDeltaTime = .1f;
}
