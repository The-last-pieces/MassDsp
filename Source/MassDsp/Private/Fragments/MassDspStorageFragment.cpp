#include "Fragments/MassDspStorageFragment.h"

EItemType FMassDspStorageFragment::TryProvideItemToSlot(int SlotIdx)
{
    auto Result = EItemType::None;
    if (InventoryCount > 0)
    {
        --InventoryCount;
        Result = StoredItemType;
        if (InventoryCount == 0)
        {
            StoredItemType = EItemType::None;
        }
    }
    return Result;
}

bool FMassDspStorageFragment::TryConsumeItemFromSlot(EItemType ItemType)
{
    if (InventoryCount >= MaxInventory) return false;
    if (InventoryCount == 0)
    {
        StoredItemType = ItemType;
        ++InventoryCount;
    }
    else if (StoredItemType == ItemType)
    {
        ++InventoryCount;
    }
    return true;
}

void FMassDspStorageFragment::TickExecute(float DeltaTime)
{
}
