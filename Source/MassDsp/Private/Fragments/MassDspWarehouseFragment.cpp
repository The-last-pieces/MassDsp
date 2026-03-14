#include "Fragments/MassDspWarehouseFragment.h"

EItemType FMassDspWarehouseFragment::TryProvideItemToSlot(int SlotIdx)
{
    int32 Removed = 0;
    return Inventory.TryRemoveAny(1, Removed);
}

bool FMassDspWarehouseFragment::TryConsumeItemFromSlot(EItemType ItemType)
{
    return Inventory.TryAddItem(ItemType, 1) > 0;
}

int32 FMassDspWarehouseFragment::TryProvideItems(EItemType ItemType, int32 MaxQty)
{
    return Inventory.TryRemoveItem(ItemType, MaxQty);
}

int32 FMassDspWarehouseFragment::TryConsumeItems(EItemType ItemType, int32 Qty)
{
    return Inventory.TryAddItem(ItemType, Qty);
}

void FMassDspWarehouseFragment::GetActiveEntries(TArray<FInventoryEntryView>& OutEntries) const
{
    Inventory.GetActiveEntries(OutEntries);
}

void FMassDspWarehouseFragment::TickExecute(float DeltaTime)
{
}