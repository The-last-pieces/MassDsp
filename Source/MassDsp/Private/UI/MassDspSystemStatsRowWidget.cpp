#include "UI/MassDspSystemStatsRowWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/MassDspSystemStatsRowData.h"

void UMassDspSystemStatsRowWidget::NativeConstruct()
{
    EnsureWidgetTreeBuilt();
    Super::NativeConstruct();
}

void UMassDspSystemStatsRowWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
    EnsureWidgetTreeBuilt();
    const UMassDspSystemStatsRowData* RowData = Cast<UMassDspSystemStatsRowData>(ListItemObject);
    if (!RowData)
    {
        ApplyTexts(FText::GetEmpty(), FText::GetEmpty(), FText::GetEmpty());
        return;
    }

    ApplyTexts(RowData->Title, RowData->Value, RowData->Details);
}

void UMassDspSystemStatsRowWidget::EnsureWidgetTreeBuilt()
{
    if (!WidgetTree || WidgetTree->RootWidget)
    {
        return;
    }

    Border_Row = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Row"));
    WidgetTree->RootWidget = Border_Row;

    FSlateBrush Brush;
    Brush.DrawAs = ESlateBrushDrawType::Box;
    Brush.TintColor = FSlateColor(FLinearColor(0.08f, 0.10f, 0.13f, 0.92f));
    Border_Row->SetBrush(Brush);
    Border_Row->SetPadding(FMargin(16.f, 12.f));

    UVerticalBox* VerticalBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VerticalBox_Row"));
    Border_Row->SetContent(VerticalBox);

    TextBlock_Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TextBlock_Title"));
    TextBlock_Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.66f, 0.74f, 0.82f, 1.f)));
    {
        FSlateFontInfo Font = TextBlock_Title->GetFont();
        Font.Size = 11;
        TextBlock_Title->SetFont(Font);
    }
    VerticalBox->AddChildToVerticalBox(TextBlock_Title);

    TextBlock_Value = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TextBlock_Value"));
    TextBlock_Value->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    {
        FSlateFontInfo Font = TextBlock_Value->GetFont();
        Font.Size = 18;
        Font.TypefaceFontName = FName("Bold");
        TextBlock_Value->SetFont(Font);
    }
    if (UVerticalBoxSlot* ValueSlot = VerticalBox->AddChildToVerticalBox(TextBlock_Value))
    {
        ValueSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 2.f));
    }

    TextBlock_Details = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TextBlock_Details"));
    TextBlock_Details->SetAutoWrapText(true);
    TextBlock_Details->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.87f, 0.92f, 0.92f)));
    {
        FSlateFontInfo Font = TextBlock_Details->GetFont();
        Font.Size = 12;
        TextBlock_Details->SetFont(Font);
    }
    VerticalBox->AddChildToVerticalBox(TextBlock_Details);
}

void UMassDspSystemStatsRowWidget::ApplyTexts(const FText& InTitle, const FText& InValue, const FText& InDetails) const
{
    if (TextBlock_Title)
    {
        TextBlock_Title->SetText(InTitle);
    }
    if (TextBlock_Value)
    {
        TextBlock_Value->SetText(InValue);
    }
    if (TextBlock_Details)
    {
        TextBlock_Details->SetText(InDetails);
    }
}