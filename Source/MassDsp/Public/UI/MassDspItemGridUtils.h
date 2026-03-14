#pragma once

#include "CoreMinimal.h"
#include "Inventory/MassDspItemInventory.h"

class UBorder;
class UGameConfigData;
class UImage;
class UMassDspItemSlotButton;
class UTextBlock;
class UUserWidget;

struct FMassDspItemGridSlotRefs
{
    UMassDspItemSlotButton* Button = nullptr;
    UBorder* Background = nullptr;
    UImage* Icon = nullptr;
    UTextBlock* Label = nullptr;
    UTextBlock* Quantity = nullptr;
};

namespace MassDspItemGridUtils
{
    void CollectGridSlots(UUserWidget* Owner, const FString& Prefix, int32 SlotCount, FName SlotGroup, TArray<FMassDspItemGridSlotRefs>& OutSlots);

    FText GetItemDisplayName(const UGameConfigData* GameConfig, EItemType ItemType);

    void ApplyGridEntries(const UGameConfigData* GameConfig,
                          const TArray<FMassDspItemGridSlotRefs>& Slots,
                          const TArray<FInventoryEntryView>& Entries,
                          bool bInteractive,
                          const FText& EmptyHint = FText());
}