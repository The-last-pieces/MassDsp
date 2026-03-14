#pragma once

#include "CoreMinimal.h"
#include "Inventory/MassDspFixedItemContainer.h"
#include "MassDspWarehouseFragment.generated.h"

USTRUCT()
struct MASSDSP_API FMassDspWarehouseFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    FFixedItemContainer Inventory;

    void Initialize(int32 InCapacity)
    {
        Inventory.Initialize(InCapacity);
    }

    int32 GetInventoryCount() const { return Inventory.GetTotalItemCount(); }
    int32 GetMaxInventory() const { return Inventory.GetCapacity(); }
    int32 GetUsedSlotCount() const { return Inventory.GetUsedSlotCount(); }
    EItemType GetFirstItemType() const { return Inventory.GetFirstItemType(); }
    int32 GetItemCount(EItemType ItemType) const { return Inventory.GetItemCount(ItemType); }

    EItemType TryProvideItemToSlot(int SlotIdx);
    bool TryConsumeItemFromSlot(EItemType ItemType);
    int32 TryProvideItems(EItemType ItemType, int32 MaxQty);
    int32 TryConsumeItems(EItemType ItemType, int32 Qty);
    void GetActiveEntries(TArray<FInventoryEntryView>& OutEntries) const;

    static void TickExecute(float DeltaTime);
};