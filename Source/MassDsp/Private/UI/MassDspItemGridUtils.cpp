#include "UI/MassDspItemGridUtils.h"

#include "Blueprint/UserWidget.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "GameConst.h"
#include "UI/MassDspItemSlotButton.h"

namespace
{
    constexpr FLinearColor EmptySlotColor(0.07f, 0.09f, 0.12f, 0.95f);
    constexpr FLinearColor FilledSlotColor(0.13f, 0.19f, 0.28f, 1.0f);

    int32 GetAdaptiveFontSize(const FText& Text)
    {
        const int32 Length = Text.ToString().Len();
        if (Length <= 2) return 18;
        if (Length <= 4) return 15;
        if (Length <= 6) return 12;
        if (Length <= 8) return 10;
        return 9;
    }

    void ApplySingleLineFont(UTextBlock* TextBlock, const FText& Text, int32 FallbackSize)
    {
        if (!TextBlock) return;

        TextBlock->SetAutoWrapText(false);
        TextBlock->SetMinDesiredWidth(0.f);

        FSlateFontInfo Font = TextBlock->GetFont();
        Font.Size = Text.IsEmpty() ? FallbackSize : GetAdaptiveFontSize(Text);
        Font.TypefaceFontName = FName("Bold");
        TextBlock->SetFont(Font);
    }

    const FItemConfigData* FindItemConfig(const UGameConfigData* GameConfig, EItemType ItemType)
    {
        return GameConfig ? GameConfig->GetItemConfig(ItemType) : nullptr;
    }
}

void MassDspItemGridUtils::CollectGridSlots(UUserWidget* Owner, const FString& Prefix, int32 SlotCount, FName SlotGroup, TArray<FMassDspItemGridSlotRefs>& OutSlots)
{
    OutSlots.Reset();
    if (!Owner) return;

    for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
    {
        const FString BaseName = FString::Printf(TEXT("%s_%d"), *Prefix, SlotIndex);

        FMassDspItemGridSlotRefs SlotRefs;
        SlotRefs.Button = Cast<UMassDspItemSlotButton>(Owner->GetWidgetFromName(*FString::Printf(TEXT("Button_%s"), *BaseName)));
        SlotRefs.Background = Cast<UBorder>(Owner->GetWidgetFromName(*FString::Printf(TEXT("Border_%s"), *BaseName)));
        SlotRefs.Icon = Cast<UImage>(Owner->GetWidgetFromName(*FString::Printf(TEXT("Image_%s"), *BaseName)));
        SlotRefs.Label = Cast<UTextBlock>(Owner->GetWidgetFromName(*FString::Printf(TEXT("TextBlock_%s_Label"), *BaseName)));
        SlotRefs.Quantity = Cast<UTextBlock>(Owner->GetWidgetFromName(*FString::Printf(TEXT("TextBlock_%s_Quantity"), *BaseName)));

        if (SlotRefs.Button)
        {
            SlotRefs.Button->SlotIndex = SlotIndex;
            SlotRefs.Button->SlotGroup = SlotGroup;
            SlotRefs.Button->BindClickForwarder();
        }

        OutSlots.Add(SlotRefs);
    }
}

FText MassDspItemGridUtils::GetItemDisplayName(const UGameConfigData* GameConfig, EItemType ItemType)
{
    if (const FItemConfigData* ItemConfig = FindItemConfig(GameConfig, ItemType))
    {
        if (!ItemConfig->DisplayName.IsEmpty())
        {
            return ItemConfig->DisplayName;
        }
    }

    const UEnum* ItemEnum = StaticEnum<EItemType>();
    return ItemEnum
        ? ItemEnum->GetDisplayNameTextByValue(static_cast<int64>(ItemType))
        : FText::FromString(TEXT("Unknown"));
}

void MassDspItemGridUtils::ApplyGridEntries(const UGameConfigData* GameConfig,
                                            const TArray<FMassDspItemGridSlotRefs>& Slots,
                                            const TArray<FInventoryEntryView>& Entries,
                                            bool bInteractive,
                                            const FText& EmptyHint)
{
    for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
    {
        const FMassDspItemGridSlotRefs& Slot = Slots[SlotIndex];
        const bool bHasEntry = Entries.IsValidIndex(SlotIndex) && Entries[SlotIndex].ItemType != EItemType::None && Entries[SlotIndex].Quantity > 0;

        if (Slot.Button)
        {
            Slot.Button->SetIsEnabled(bInteractive && bHasEntry);
        }

        if (Slot.Background)
        {
            Slot.Background->SetBrushColor(bHasEntry ? FilledSlotColor : EmptySlotColor);
        }

        if (!bHasEntry)
        {
            if (Slot.Icon)
            {
                Slot.Icon->SetBrushFromTexture(nullptr);
                Slot.Icon->SetVisibility(ESlateVisibility::Collapsed);
            }

            if (Slot.Quantity)
            {
                Slot.Quantity->SetText(FText::GetEmpty());
                Slot.Quantity->SetVisibility(ESlateVisibility::Collapsed);
            }

            if (Slot.Label)
            {
                Slot.Label->SetText(FText::GetEmpty());
                Slot.Label->SetVisibility(ESlateVisibility::Collapsed);
                ApplySingleLineFont(Slot.Label, EmptyHint, 10);
            }
            continue;
        }

        const FInventoryEntryView& Entry = Entries[SlotIndex];
        const FItemConfigData* ItemConfig = FindItemConfig(GameConfig, Entry.ItemType);
        const FText DisplayName = GetItemDisplayName(GameConfig, Entry.ItemType);
        const bool bHasIcon = ItemConfig && ItemConfig->Icon;

        if (Slot.Icon)
        {
            Slot.Icon->SetBrushFromTexture(bHasIcon ? ItemConfig->Icon : nullptr, true);
            Slot.Icon->SetVisibility(bHasIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        }

        if (Slot.Label)
        {
            Slot.Label->SetText(DisplayName);
            Slot.Label->SetVisibility(bHasIcon ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
            ApplySingleLineFont(Slot.Label, DisplayName, 10);
        }

        if (Slot.Quantity)
        {
            Slot.Quantity->SetText(FText::AsNumber(Entry.Quantity));
            Slot.Quantity->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
    }
}