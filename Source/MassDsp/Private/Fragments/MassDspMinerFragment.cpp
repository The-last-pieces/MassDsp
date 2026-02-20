#include "Fragments/MassDspMinerFragment.h"

EItemType FMassDspMinerFragment::TryProvideItemToSlot(int SlotIdx)
{
    if (InventoryCount > 0)
    {
        --InventoryCount;
        return StoredItemType;
    }
    return EItemType::None;
}

bool FMassDspMinerFragment::TryConsumeItemFromSlot(EItemType ItemType)
{
    return false;
}

void FMassDspMinerFragment::TickExecute(float DeltaTime)
{
    if (InventoryCount < MaxInventory)
    {
        ProductionProgress += DeltaTime / (ProductionInterval > 0 ? ProductionInterval : 1.0f);
        if (auto ProgressInt = FMath::FloorToInt(ProductionProgress); ProgressInt >= 1)
        {
            InventoryCount = FMath::Min(InventoryCount + ProgressInt, MaxInventory);
            ProductionProgress -= ProgressInt;
        }
    }
}
