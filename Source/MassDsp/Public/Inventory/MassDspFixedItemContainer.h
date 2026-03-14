#pragma once

#include "CoreMinimal.h"
#include "Inventory/MassDspItemInventory.h"

#include "MassDspFixedItemContainer.generated.h"

USTRUCT()
struct MASSDSP_API FFixedItemContainerSlot
{
    GENERATED_BODY()

    UPROPERTY()
    EItemType ItemType = EItemType::None;

    UPROPERTY()
    int32 Quantity = 0;
};

USTRUCT()
struct MASSDSP_API FFixedItemContainer
{
    GENERATED_BODY()

public:
    enum { MaxSlotCount = 8 };

    void Initialize(int32 InMaxTotalItems)
    {
        MaxTotalItems = FMath::Max(0, InMaxTotalItems);
        TotalItemCount = 0;
        UsedSlotCount = 0;
        for (FFixedItemContainerSlot& Slot : Slots)
        {
            Slot.ItemType = EItemType::None;
            Slot.Quantity = 0;
        }
    }

    int32 GetCapacity() const { return MaxTotalItems; }
    int32 GetTotalItemCount() const { return TotalItemCount; }
    int32 GetFreeCapacity() const { return FMath::Max(0, MaxTotalItems - TotalItemCount); }
    int32 GetUsedSlotCount() const { return UsedSlotCount; }

    int32 GetItemCount(EItemType ItemType) const
    {
        const int32 SlotIndex = FindSlotIndex(ItemType);
        return SlotIndex != INDEX_NONE ? Slots[SlotIndex].Quantity : 0;
    }

    EItemType GetFirstItemType() const
    {
        return UsedSlotCount > 0 ? Slots[0].ItemType : EItemType::None;
    }

    int32 TryAddItem(EItemType ItemType, int32 Quantity)
    {
        if (ItemType == EItemType::None || Quantity <= 0) return 0;

        const int32 Accepted = FMath::Min(Quantity, GetFreeCapacity());
        if (Accepted <= 0) return 0;

        int32 SlotIndex = FindSlotIndex(ItemType);
        if (SlotIndex == INDEX_NONE)
        {
            if (UsedSlotCount >= MaxSlotCount) return 0;
            SlotIndex = UsedSlotCount++;
            Slots[SlotIndex].ItemType = ItemType;
            Slots[SlotIndex].Quantity = 0;
        }

        Slots[SlotIndex].Quantity += Accepted;
        TotalItemCount += Accepted;
        return Accepted;
    }

    int32 TryRemoveItem(EItemType ItemType, int32 Quantity)
    {
        if (ItemType == EItemType::None || Quantity <= 0) return 0;

        const int32 SlotIndex = FindSlotIndex(ItemType);
        if (SlotIndex == INDEX_NONE) return 0;

        const int32 Removed = FMath::Min(Quantity, Slots[SlotIndex].Quantity);
        if (Removed <= 0) return 0;

        Slots[SlotIndex].Quantity -= Removed;
        TotalItemCount -= Removed;
        if (Slots[SlotIndex].Quantity == 0)
        {
            RemoveSlotAt(SlotIndex);
        }
        return Removed;
    }

    EItemType TryRemoveAny(int32 Quantity, int32& OutRemoved)
    {
        OutRemoved = 0;
        if (Quantity <= 0 || UsedSlotCount <= 0) return EItemType::None;

        FFixedItemContainerSlot& Slot = Slots[0];
        const EItemType ItemType = Slot.ItemType;
        OutRemoved = TryRemoveItem(ItemType, Quantity);
        return OutRemoved > 0 ? ItemType : EItemType::None;
    }

    void GetActiveEntries(TArray<FInventoryEntryView>& OutEntries) const
    {
        OutEntries.Reset(UsedSlotCount);
        for (int32 SlotIndex = 0; SlotIndex < UsedSlotCount; ++SlotIndex)
        {
            const FFixedItemContainerSlot& Slot = Slots[SlotIndex];
            if (Slot.ItemType == EItemType::None || Slot.Quantity <= 0) continue;

            FInventoryEntryView Entry;
            Entry.ItemType = Slot.ItemType;
            Entry.Quantity = Slot.Quantity;
            OutEntries.Add(Entry);
        }
    }

private:
    int32 FindSlotIndex(EItemType ItemType) const
    {
        for (int32 SlotIndex = 0; SlotIndex < UsedSlotCount; ++SlotIndex)
        {
            if (Slots[SlotIndex].ItemType == ItemType)
            {
                return SlotIndex;
            }
        }
        return INDEX_NONE;
    }

    void RemoveSlotAt(int32 SlotIndex)
    {
        if (SlotIndex < 0 || SlotIndex >= UsedSlotCount) return;

        const int32 LastIndex = UsedSlotCount - 1;
        if (SlotIndex != LastIndex)
        {
            Slots[SlotIndex] = Slots[LastIndex];
        }

        Slots[LastIndex].ItemType = EItemType::None;
        Slots[LastIndex].Quantity = 0;
        --UsedSlotCount;
    }

    UPROPERTY()
    int32 MaxTotalItems = 0;

    UPROPERTY()
    int32 TotalItemCount = 0;

    UPROPERTY()
    int32 UsedSlotCount = 0;

    UPROPERTY()
    FFixedItemContainerSlot Slots[MaxSlotCount];
};