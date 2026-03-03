#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "MassDspStorageFragment.generated.h"

/**
 * 仓库特定的Fragment
 * 包含仓库的存储相关数据
 */
USTRUCT()
struct MASSDSP_API FMassDspStorageFragment : public FMassFragment
{
    GENERATED_BODY()

    // 当前存储数量
    UPROPERTY()
    int32 InventoryCount = 0;

    // 存储容量
    UPROPERTY()
    int32 MaxInventory = 50;

    // 当前储存类型
    UPROPERTY()
    EItemType StoredItemType = EItemType::None;

    EItemType TryProvideItemToSlot(int SlotIdx);

    bool TryConsumeItemFromSlot(EItemType ItemType);

    /** 批量取出最多 MaxQty 个同类物品，返回实际取出数量 */
    int32 TryProvideItems(int32 MaxQty);

    /** 批量存入最多 Qty 个 ItemType，返回实际存入数量 */
    int32 TryConsumeItems(EItemType ItemType, int32 Qty);

    static void TickExecute(float DeltaTime);
};
