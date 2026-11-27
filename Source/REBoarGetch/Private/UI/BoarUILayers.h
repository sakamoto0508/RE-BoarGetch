#pragma once

#include "CoreMinimal.h"

/**
 * 画面（Widget）の重なり順と、Input Mapping Contextの優先度をまとめた定数です。
 * 値どうしの大小関係で前後・優先が決まるため、新しい画面や操作を足すときはここに名前を追加し、既存の値との関係を確認してください。
 * ACF Transitionsの遷移Overlay（Project SettingsのOverlay ZOrder、既定10000）は、これらすべてより前面に出ます。
 */
namespace BoarUILayer
{
	/** 通常のHUD。最背面。 */
	constexpr int32 HUD = 0;

	/** HUDの前に出す演出・選択画面（Stage Clearバナー、GameOverの画面効果、Lobbyのステージ選択）。 */
	constexpr int32 Presentation = 50;

	/** Pauseメニュー。 */
	constexpr int32 PauseMenu = 80;

	/** 装備変更（Pauseから開く場合はPauseより前）。 */
	constexpr int32 Loadout = 90;

	/** メニュー画面（設定・施設メニュー・展示の選択）。 */
	constexpr int32 Menu = 100;

	/** Stage終了画面（Result・GameOver）。メニューと同じ高さで、同時には開かない。 */
	constexpr int32 EndScreen = 100;

	/** Lobbyへ戻る確認ダイアログ（Pauseの上に出す）。 */
	constexpr int32 Confirmation = 110;

	/** 図鑑（メニューの上に開く）。 */
	constexpr int32 Encyclopedia = 120;
}

/** Input Mapping Contextの優先度。数値が大きいほど優先され、同じキーを後から上書きできます。 */
namespace BoarInputPriority
{
	/** 通常操作（移動・Gadgetなど）。 */
	constexpr int32 Default = 0;

	/** Pauseなど、どの画面でも有効な操作。 */
	constexpr int32 Global = 10;

	/** Lobby・施設だけの操作（Returnなど）。 */
	constexpr int32 Facility = 15;

	/** メニューを開いている間のUI操作。 */
	constexpr int32 UI = 20;
}

// 前後関係が崩れたらビルドで気付けるようにする（同じ高さのMenu・EndScreenは同時に開かない前提）。
static_assert(BoarUILayer::HUD < BoarUILayer::Presentation && BoarUILayer::Presentation < BoarUILayer::PauseMenu
	&& BoarUILayer::PauseMenu < BoarUILayer::Loadout && BoarUILayer::Loadout < BoarUILayer::Menu
	&& BoarUILayer::Menu < BoarUILayer::Confirmation && BoarUILayer::Confirmation < BoarUILayer::Encyclopedia,
	"BoarUILayerの前後関係が崩れています");
static_assert(BoarInputPriority::Default < BoarInputPriority::Global && BoarInputPriority::Global < BoarInputPriority::Facility
	&& BoarInputPriority::Facility < BoarInputPriority::UI, "BoarInputPriorityの優先順が崩れています");
