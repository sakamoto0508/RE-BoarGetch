#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarResultBoarCard.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

/** Result画面「新規登録ボア」の1枠です。画像エリア・番号・名前・補足・NEWタグを別Widgetに分け、写真が未設定でも差し替え用プレースホルダーを表示します。空き枠（未登録スロット）表示にも使います。 */
UCLASS(Blueprintable)
class REBOARGETCH_API UBoarResultBoarCard : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 登録ボアを表示します。PhotoがnullならPlaceholderPhotoTextureを使います。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Result Card")
	void SetBoarEntry(UTexture2D* Photo, const FText& Number, const FText& Name, const FText& SubText, bool bIsNew);
	/** 未登録スロット（番号・名前は「---」、画像は空き枠）として表示します。 */
	UFUNCTION(BlueprintCallable, Category = "REBoarGetch|Result Card")
	void SetEmptySlot();

protected:
	/** 新規登録個体のカードを囲む枠画像。 */
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional))
	TObjectPtr<UImage> CardFrame;

	/** 新規登録個体の写真を表示する画像。 */
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Photo;

	/** 個体写真の四隅を飾る画像。 */
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional))
	TObjectPtr<UImage> PhotoCorners;

	/** 個体の図鑑番号を表示するテキスト。 */
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NumberText;

	/** 個体の表示名を表示するテキスト。 */
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	/** 個体の補足情報を表示するテキスト。 */
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubText;

	/** NEWタグ（画像＋文字のまとまり）。NEWのときだけ表示します。 */
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> NewTag;

	/** 枠：NEW（アンバー発光）／登録済み（シアン）／空き枠。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures")
	TObjectPtr<UTexture2D> FrameNewTexture;

	/** 個体の登録がある通常枠の画像。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures")
	TObjectPtr<UTexture2D> FrameNormalTexture;

	/** 個体の登録がない空き枠の画像。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures")
	TObjectPtr<UTexture2D> FrameEmptyTexture;

	/** 画像エリア四隅のマーク：NEW用／通常用／空き枠用。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures")
	TObjectPtr<UTexture2D> CornersNewTexture;

	/** 個体の登録がある写真の四隅の画像。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures")
	TObjectPtr<UTexture2D> CornersNormalTexture;

	/** 空き枠に表示する写真の四隅の画像。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures")
	TObjectPtr<UTexture2D> CornersEmptyTexture;

	/** 図鑑写真が未設定の個体に出す差し替え用プレースホルダーと、空き枠の画像。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures")
	TObjectPtr<UTexture2D> PlaceholderPhotoTexture;

	/** 個体の写真を表示できない場合の画像。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures")
	TObjectPtr<UTexture2D> EmptyPhotoTexture;

	/** 通常枠の図鑑番号の文字色。 */
	UPROPERTY(EditDefaultsOnly, Category = "Style")
	FLinearColor NumberColor = FLinearColor(.28f, .72f, .9f, 1.f);

	/** 通常枠の個体名の文字色。 */
	UPROPERTY(EditDefaultsOnly, Category = "Style")
	FLinearColor NameColor = FLinearColor(.9f, .97f, 1.f, 1.f);

	/** 通常枠の補足文字の色。 */
	UPROPERTY(EditDefaultsOnly, Category = "Style")
	FLinearColor SubColor = FLinearColor(.22f, .6f, .78f, 1.f);

	/** 空き枠に表示する文字色。 */
	UPROPERTY(EditDefaultsOnly, Category = "Style")
	FLinearColor EmptyTextColor = FLinearColor(.2f, .42f, .55f, 1.f);

	/** 名前の文字数がこれを超えたら（図鑑名未設定でIDを出す場合など）文字サイズを小さくします。 */
	UPROPERTY(EditDefaultsOnly, Category = "Style", meta = (ClampMin = "1"))
	int32 LongNameCharacters = 8;

	/** 長い個体名を収める縮小後のフォントサイズ。 */
	UPROPERTY(EditDefaultsOnly, Category = "Style", meta = (ClampMin = "6"))
	int32 LongNameFontSize = 12;

	/** 空き枠の図鑑番号欄に表示する文言。 */
	UPROPERTY(EditDefaultsOnly, Category = "Style")
	FText EmptyNumberText = NSLOCTEXT("Result", "CardEmptyNo", "No.---");

	/** 空き枠の個体名欄に表示する文言。 */
	UPROPERTY(EditDefaultsOnly, Category = "Style")
	FText EmptyNameText = NSLOCTEXT("Result", "CardEmptyName", "---");

	/** 空き枠の補足欄に表示する文言。 */
	UPROPERTY(EditDefaultsOnly, Category = "Style")
	FText EmptySubText = NSLOCTEXT("Result", "CardEmptySub", "未登録スロット");

	virtual void NativeConstruct() override;

private:
	void SetTexture(UImage* Image, UTexture2D* Texture) const;
	/** WBPで設定した名前の文字サイズ（長い名前を縮めた後に戻すため）。 */
	int32 DefaultNameFontSize = 0;
};
