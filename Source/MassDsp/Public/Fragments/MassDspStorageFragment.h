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

    static void TickExecute(float DeltaTime);
};
