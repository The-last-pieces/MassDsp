#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "MassDspMinerFragment.generated.h"

/**
 * 矿机特定的Fragment
 * 包含矿机的生产逻辑相关数据
 */
USTRUCT()
struct MASSDSP_API FMassDspMinerFragment : public FMassFragment
{
    GENERATED_BODY()

    // 下次生产触发的绝对世界时间（0 = 尚未初始化，第一帧会自动设置）
    UPROPERTY()
    float NextProductionWorldTime = 0.0f;

    // 生产间隔（秒）
    UPROPERTY()
    float ProductionInterval = 2.0f;

    // 当前库存数量（生产缓冲区）
    UPROPERTY()
    int32 InventoryCount = 0;

    // 最大库存容量
    UPROPERTY()
    int32 MaxInventory = 50;

    // 存储类型
    UPROPERTY()
    EItemType StoredItemType = EItemType::None;

    EItemType TryProvideItemToSlot(int SlotIdx);

    static bool TryConsumeItemFromSlot(EItemType ItemType);

    // 参数为当前世界绝对时间（World->GetTimeSeconds()）
    // 大多数帧内因未到触发时刻而立即返回，避免无意义的浮点除法
    void TickExecute(float WorldTime);

    float GetTheoreticalProductionRate() const
    {
        if (StoredItemType == EItemType::None || ProductionInterval <= KINDA_SMALL_NUMBER || InventoryCount >= MaxInventory)
        {
            return 0.f;
        }
        return 1.f / ProductionInterval;
    }

    // 返回 [0,1] 的生产进度（供 UI 进度条使用），WorldTime = World->GetTimeSeconds()
    float GetProductionProgress(float WorldTime) const
    {
        if (NextProductionWorldTime <= 0.f || ProductionInterval <= 0.f) return 0.f;
        const float Remaining = NextProductionWorldTime - WorldTime;
        return FMath::Clamp(1.f - Remaining / ProductionInterval, 0.f, 1.f);
    }
};
