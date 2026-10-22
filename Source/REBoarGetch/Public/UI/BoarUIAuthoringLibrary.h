#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BoarUIAuthoringLibrary.generated.h"

class UWidgetBlueprint;

/**
 * Editor専用：Widget BlueprintへTextBlockとUMGアニメーションを追加する自動化用関数です。
 * PythonからResult画面の順表示アニメを再現可能に作成するために使用し、ゲーム実行時には含まれません。
 */
UCLASS()
class REBOARGETCH_API UBoarUIAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/** Widget Tree（名前・Class・Slot・Text）とアニメーション名を文字列で返します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static FString DescribeWidgetBlueprint(UObject* WidgetBlueprint);

	/** 指定PanelへTextBlockを追加（既存なら更新）します。StyleSourceのFont/色を複製しFontSizeだけ上書きします。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static bool EnsureTextBlock(UObject* WidgetBlueprint, FName WidgetName, FName ParentPanelName, int32 InsertIndex,
		FName StyleSourceName, const FText& Text, int32 FontSize, FLinearColor Color);

	/** 不透明度0→1、拡大→等倍、下からのSlideで登場するアニメを作成（既存は作り直し）します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static bool EnsureRevealAnimation(UObject* WidgetBlueprint, FName AnimationName, FName WidgetName,
		float Duration, float StartScale, float StartOffsetY);

	/** 検証用：Game Viewの次フレームをPNGで保存します（bShowUIでUMGを含めます）。要求中ならfalse。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Verification")
	static bool RequestVerificationScreenshot(const FString& AbsolutePngPath, bool bShowUI);
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Editor|Verification")
	static bool IsVerificationScreenshotPending();
	/** 検証用：次回PIEを指定サイズの独立ウィンドウで開きます（設定ファイルには保存しません）。RestorePIEWindowで元に戻します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Verification")
	static void UsePIENewWindow(int32 Width, int32 Height);
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Verification")
	static void RestorePIEWindow();
	/** 検証用：現在のLevelでPIEを独立ウィンドウ（指定サイズ）で開始します。一時Play設定を使い、エディタ設定は変更しません。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Verification")
	static void RequestPIEInNewWindow(int32 Width, int32 Height);
	/** 検証用：PIE World に指定IDの特別コイン（C++基底）を生成します。Resultのコイン数が実データ由来か確認するために使います。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Verification", meta = (WorldContext = "WorldContextObject"))
	static class ASpecialCoin* SpawnVerificationCoin(UObject* WorldContextObject, FName CoinId, FVector Location);
#endif
};
