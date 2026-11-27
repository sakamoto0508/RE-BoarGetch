#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/Widget.h"
#include "BoarUIAuthoringLibrary.generated.h"

class UWidgetBlueprint;

// Editor専用：Widget BlueprintへTextBlockとUMGアニメーションを追加する自動化用関数です。
// PythonからResult画面の順表示アニメを再現可能に作成するために使用し、ゲーム実行時には含まれません。
/** EditorでWidget Blueprintに表示要素やアニメーションを追加する。 */
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

	/** 既存TextBlockの文字・書体（Typeface名、例 "Bold"）・色・アウトライン・影を設定します。FontSize<=0は維持します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static bool ConfigureTextBlockStyle(UObject* WidgetBlueprint, FName WidgetName, const FText& Text, int32 FontSize, FName Typeface,
		FLinearColor Color, int32 OutlineSize, FLinearColor OutlineColor, FVector2D ShadowOffset, FLinearColor ShadowColor);

	/** 指定Class・名前のWidgetを取得し、無ければ作成します。ParentPanelNameがNoneで木が空ならRootにします。bIsVariableでC++のBindWidget対象にします。返り値のPropertyはPythonから設定します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static UWidget* EnsureWidget(UObject* WidgetBlueprint, TSubclassOf<UWidget> WidgetClass, FName WidgetName, FName ParentPanelName, bool bIsVariable);

	/** 指定Widgetを子ごとWidget Treeから取り除きます。Rootは取り除けません。見つからなければfalse。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static bool RemoveWidget(UObject* WidgetBlueprint, FName WidgetName);

	/** Event Graph・関数グラフのノード名を列挙します（削除はしません）。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static FString DescribeBlueprintGraphs(UObject* Blueprint);

	/** Event Graphの全ノードとユーザー関数グラフを削除し、削除したノード・関数を列挙して返します（処理をC++へ移した画面用）。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static FString ClearBlueprintGraphs(UObject* Blueprint);

	/**
	 * 関数呼び出しノード（OldFunctionName）の呼び出し先をNewOwnerClass::NewFunctionNameへ差し替えます。
	 * 同じ名前のピンは接続と既定値を保ちます。差し替えたノードの数を返します。
	 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static int32 RetargetFunctionCalls(UObject* Blueprint, FName OldFunctionName, UClass* NewOwnerClass, FName NewFunctionName);

	/**
	 * 配置済みActorを、その子クラス（BPなど）のActorへ置き換えます。名前・ラベル・フォルダ・Transform・アタッチ・
	 * 編集済みのプロパティ（コンポーネントを含む）を引き継ぎ、同じLevelの他のActorやLevel Blueprintからの参照も新しいActorへ付け替えます。
	 * 置き換えた結果（失敗・付け替えた参照の数）を文字列で返します。
	 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Level")
	static FString ReplaceActorsWithSubclass(const TArray<AActor*>& Actors, TSubclassOf<AActor> NewClass);

	/**
	 * Level上の部品ActorをBlueprintのComponentとして取り込みます（Levelの部品は削除しません）。
	 * 位置はPivot（そのBPの配置Actor）からの相対Transformに変換します。bAsChildActorsがfalseなら各部品のRoot Componentを複製し、
	 * trueならChild Actor Componentとして取り込みます。作成したComponent名を返します。
	 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Level")
	static TArray<FName> AbsorbActorsIntoBlueprint(UBlueprint* Blueprint, AActor* Pivot, const TArray<AActor*>& Parts, bool bAsChildActors);

	/** BPのSCSに空のScene Component（RootName）をRootとして作り、既存のSCS Componentをすべてその直下へ付け直します（相対Transformは維持）。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Level")
	static bool RebuildBlueprintSceneRoot(UBlueprint* Blueprint, FName RootName);

	/**
	 * DescribeEditablePropertiesの出力（Prefix.Property=Value の行）から、BPへComponentを1つ復元します。
	 * ParentName（SCS）の下に付け、相対Transformは引数の値を使います。
	 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Level")
	static bool RestoreBlueprintComponentFromDescription(UBlueprint* Blueprint, FName ParentName, TSubclassOf<USceneComponent> ComponentClass,
		FName ComponentName, const FString& Description, const FString& Prefix, FTransform Relative);

	/** Blueprintの指定Component（SCS）のComponent Tagsに、Tagを追加します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Level")
	static bool AddBlueprintComponentTag(UBlueprint* Blueprint, FName ComponentName, FName Tag);

	/** 同じLevel内（Level Blueprintを含む）でTargetsを参照しているObjectを列挙します（削除前の確認用）。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Level")
	static FString FindActorReferencers(const TArray<AActor*>& Targets);

	/** Actorとそのコンポーネントの編集可能なプロパティをテキストで返します（置き換え前後の比較用）。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Level")
	static FString DescribeEditableProperties(AActor* Actor);

	/** CanvasPanelSlot上のWidgetの位置・サイズ・AutoSizeを設定します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static bool SetCanvasSlotLayout(UObject* WidgetBlueprint, FName WidgetName, FVector2D Position, FVector2D Size, bool bAutoSize);

	/** 不透明度0→1、拡大→等倍、下からのSlideで登場するアニメを作成（既存は作り直し）します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static bool EnsureRevealAnimation(UObject* WidgetBlueprint, FName AnimationName, FName WidgetName,
		float Duration, float StartScale, float StartOffsetY);

	/** Periodごとに不透明度1→OffOpacityを切り替える点滅アニメを作成（既存は作り直し）します。ループ再生はWidget側で行います。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static bool EnsureBlinkAnimation(UObject* WidgetBlueprint, FName AnimationName, FName WidgetName,
		float Period, float OnDuration, float OffOpacity);

	/** 検証用：Game Viewの次フレームをPNGで保存します（bShowUIでUMGを含めます）。要求中ならfalse。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Verification")
	static bool RequestVerificationScreenshot(const FString& AbsolutePngPath, bool bShowUI);
	UFUNCTION(BlueprintPure, Category = "REBoarGetch|Editor|Verification")
	static bool IsVerificationScreenshotPending();
	/** フォントファイル（TTF/OTF）からFontFaceアセットを作成または更新します（Import Factoryを使わないのでヘッドレスでも動きます）。保存は呼び出し側。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static class UFontFace* EnsureFontFaceFromFile(const FString& PackagePath, const FString& FontFilePath);
	/** 取り込み済みFontFaceから、Runtimeキャッシュの複合Font（Typeface名ごと）を作成または更新します。保存は呼び出し側で行います。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|UI")
	static class UFont* EnsureCompositeFont(const FString& PackagePath, const TArray<FName>& TypefaceNames, const TArray<class UFontFace*>& Faces);
	/** 検証用：Widget中央へ左クリック（押下→離す）をSlateへ送ります。実際のヒットテスト経由で届くかを確認できます。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Verification")
	static bool SimulateLeftClickOnWidget(UWidget* Widget);
	/** 検証用：Widget中央付近でマウスを動かす入力をSlateへ送ります（ボタンは押しません）。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Verification")
	static bool SimulateMouseMoveOverWidget(UWidget* Widget);
	/** 検証用：キーの押下→離すをSlateへ送ります（フォーカス経路・PreviewKeyDownを通ります）。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Editor|Verification")
	static bool SimulateKeyPress(FKey Key);
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
