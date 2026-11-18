#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BoarResultBoarCard.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

/**
 * Result画面「新規登録ボア」の1枠です。画像エリア・番号・名前・補足・NEWタグを別Widgetに分け、
 * 写真が未設定でも差し替え用プレースホルダーを表示します。空き枠（未登録スロット）表示にも使います。
 */
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
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional)) TObjectPtr<UImage> CardFrame;
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional)) TObjectPtr<UImage> Photo;
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional)) TObjectPtr<UImage> PhotoCorners;
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NumberText;
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NameText;
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SubText;
	/** NEWタグ（画像＋文字のまとまり）。NEWのときだけ表示します。 */
	UPROPERTY(BlueprintReadOnly, Category = "Bindings", meta = (BindWidgetOptional)) TObjectPtr<UWidget> NewTag;

	/** 枠：NEW（アンバー発光）／登録済み（シアン）／空き枠。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures") TObjectPtr<UTexture2D> FrameNewTexture;
	UPROPERTY(EditDefaultsOnly, Category = "Textures") TObjectPtr<UTexture2D> FrameNormalTexture;
	UPROPERTY(EditDefaultsOnly, Category = "Textures") TObjectPtr<UTexture2D> FrameEmptyTexture;
	/** 画像エリア四隅のマーク：NEW用／通常用／空き枠用。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures") TObjectPtr<UTexture2D> CornersNewTexture;
	UPROPERTY(EditDefaultsOnly, Category = "Textures") TObjectPtr<UTexture2D> CornersNormalTexture;
	UPROPERTY(EditDefaultsOnly, Category = "Textures") TObjectPtr<UTexture2D> CornersEmptyTexture;
	/** 図鑑写真が未設定の個体に出す差し替え用プレースホルダーと、空き枠の画像。 */
	UPROPERTY(EditDefaultsOnly, Category = "Textures") TObjectPtr<UTexture2D> PlaceholderPhotoTexture;
	UPROPERTY(EditDefaultsOnly, Category = "Textures") TObjectPtr<UTexture2D> EmptyPhotoTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Style") FLinearColor NumberColor = FLinearColor(.28f, .72f, .9f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Style") FLinearColor NameColor = FLinearColor(.9f, .97f, 1.f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Style") FLinearColor SubColor = FLinearColor(.22f, .6f, .78f, 1.f);
	UPROPERTY(EditDefaultsOnly, Category = "Style") FLinearColor EmptyTextColor = FLinearColor(.2f, .42f, .55f, 1.f);
	/** 名前の文字数がこれを超えたら（図鑑名未設定でIDを出す場合など）文字サイズを小さくします。 */
	UPROPERTY(EditDefaultsOnly, Category = "Style", meta = (ClampMin = "1")) int32 LongNameCharacters = 8;
	UPROPERTY(EditDefaultsOnly, Category = "Style", meta = (ClampMin = "6")) int32 LongNameFontSize = 12;
	UPROPERTY(EditDefaultsOnly, Category = "Style") FText EmptyNumberText = NSLOCTEXT("Result", "CardEmptyNo", "No.---");
	UPROPERTY(EditDefaultsOnly, Category = "Style") FText EmptyNameText = NSLOCTEXT("Result", "CardEmptyName", "---");
	UPROPERTY(EditDefaultsOnly, Category = "Style") FText EmptySubText = NSLOCTEXT("Result", "CardEmptySub", "未登録スロット");

	virtual void NativeConstruct() override;
private:
	void SetTexture(UImage* Image, UTexture2D* Texture) const;
	/** WBPで設定した名前の文字サイズ（長い名前を縮めた後に戻すため）。 */
	int32 DefaultNameFontSize = 0;
};
