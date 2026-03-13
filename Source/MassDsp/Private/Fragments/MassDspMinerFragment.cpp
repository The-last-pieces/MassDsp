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

void FMassDspMinerFragment::TickExecute(float WorldTime)
{
    if (InventoryCount >= MaxInventory)
    {
        NextProductionWorldTime = 0.f;
        return;
    }

    // 第一帧初始化计时器，避免放置时立即产出
    if (NextProductionWorldTime <= 0.f)
    {
        NextProductionWorldTime = WorldTime + ProductionInterval;
        return;
    }

    if (WorldTime < NextProductionWorldTime) return;

    // 计算本帧应批量产出多少（追帧补产，如跳帧或暂停后恢复）
    const int32 BatchCount = FMath::Min(
        FMath::FloorToInt((WorldTime - NextProductionWorldTime) / ProductionInterval) + 1,
        MaxInventory - InventoryCount
    );
    InventoryCount += BatchCount;
    NextProductionWorldTime += BatchCount * ProductionInterval;
}
