#include "UI/BoarResultBoarCard.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UBoarResultBoarCard::NativeConstruct()
{
	Super::NativeConstruct();
	if (NameText) DefaultNameFontSize = NameText->GetFont().Size;
}

void UBoarResultBoarCard::SetTexture(UImage* Image, UTexture2D* Texture) const
{
	if (Image && Texture) Image->SetBrushFromTexture(Texture, false);
}

void UBoarResultBoarCard::SetBoarEntry(UTexture2D* InPhoto, const FText& Number, const FText& Name, const FText& InSubText,
                                       bool bIsNew)
{
	SetTexture(CardFrame, bIsNew ? FrameNewTexture : FrameNormalTexture);
	SetTexture(PhotoCorners, bIsNew ? CornersNewTexture : CornersNormalTexture);
	SetTexture(Photo, InPhoto ? InPhoto : PlaceholderPhotoTexture.Get());
	if (NumberText)
	{
		NumberText->SetText(Number);
		NumberText->SetColorAndOpacity(FSlateColor(NumberColor));
	}
	if (NameText)
	{
		NameText->SetText(Name);
		NameText->SetColorAndOpacity(FSlateColor(NameColor));
		if (DefaultNameFontSize <= 0) DefaultNameFontSize = NameText->GetFont().Size;
		FSlateFontInfo Font = NameText->GetFont();
		Font.Size = Name.ToString().Len() > LongNameCharacters ? LongNameFontSize : DefaultNameFontSize;
		NameText->SetFont(Font);
	}
	if (SubText)
	{
		SubText->SetText(InSubText);
		SubText->SetColorAndOpacity(FSlateColor(SubColor));
	}
	if (NewTag) NewTag->SetVisibility(bIsNew ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UBoarResultBoarCard::SetEmptySlot()
{
	SetTexture(CardFrame, FrameEmptyTexture);
	SetTexture(PhotoCorners, CornersEmptyTexture);
	SetTexture(Photo, EmptyPhotoTexture);
	for (UTextBlock* Text : {NumberText.Get(), NameText.Get(), SubText.Get()})
		if (Text) Text->SetColorAndOpacity(FSlateColor(EmptyTextColor));
	if (NumberText) NumberText->SetText(EmptyNumberText);
	if (NameText)
	{
		NameText->SetText(EmptyNameText);
		if (DefaultNameFontSize > 0)
		{
			FSlateFontInfo Font = NameText->GetFont();
			Font.Size = DefaultNameFontSize;
			NameText->SetFont(Font);
		}
	}
	if (SubText) SubText->SetText(EmptySubText);
	if (NewTag) NewTag->SetVisibility(ESlateVisibility::Collapsed);
}
