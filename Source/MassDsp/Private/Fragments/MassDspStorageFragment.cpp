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

int32 FMassDspStorageFragment::TryProvideItems(int32 MaxQty)
{
    if (InventoryCount <= 0 || MaxQty <= 0) return 0;
    const int32 Taken = FMath::Min(MaxQty, InventoryCount);
    InventoryCount -= Taken;
    if (InventoryCount == 0)
        StoredItemType = EItemType::None;
    return Taken;
}

int32 FMassDspStorageFragment::TryConsumeItems(EItemType ItemType, int32 Qty)
{
    if (Qty <= 0 || ItemType == EItemType::None) return 0;
    if (InventoryCount > 0 && StoredItemType != ItemType) return 0; // 类型不匹配
    const int32 Storable = FMath::Max(0, MaxInventory - InventoryCount);
    const int32 Stored   = FMath::Min(Qty, Storable);
    if (Stored <= 0) return 0;
    StoredItemType  = ItemType;
    InventoryCount += Stored;
    return Stored;
}

void FMassDspStorageFragment::TickExecute(float DeltaTime)
{
}
