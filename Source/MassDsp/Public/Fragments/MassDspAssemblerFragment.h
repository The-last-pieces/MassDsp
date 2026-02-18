#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "GameConst.h"
#include "MassDspAssemblerFragment.generated.h"

/**
 * 合成台输入槽位缓冲数据
 */
USTRUCT()
struct FAssemblerInputBuffer
{
    GENERATED_BODY()

    // 物品类型
    UPROPERTY()
    EItemType ItemType = EItemType::None;

    // 当前数量
    UPROPERTY()
    int32 Count = 0;
};

/**
 * 合成台特定的Fragment
 * 包含合成台的配方、进度和缓冲区数据
 */
USTRUCT()
struct MASSDSP_API FMassDspAssemblerFragment : public FMassFragment
{
    GENERATED_BODY()

    // 当前配方
    UPROPERTY()
    FRecipeDataForFragment CurrentRecipe;

    // 当前合成进度 (0.0 - 1.0)
    UPROPERTY()
    float CraftingProgress = 0.0f;

    // 合成速度倍率
    UPROPERTY()
    float CraftingSpeedMultiplier = 1.0f;

    // 输入缓冲区容量（每个槽位）
    UPROPERTY()
    int32 InputBufferCapacity = 10;

    // 输出缓冲区容量
    UPROPERTY()
    int32 OutputBufferCapacity = 10;

    // 输入缓冲区（最多3个槽位）
    UPROPERTY()
    FAssemblerInputBuffer InputBuffers[3];

    // 输出缓冲区当前数量
    UPROPERTY()
    int32 OutputBufferCount = 0;

    // 最大输出库存容量（用于槽口逻辑）
    UPROPERTY()
    int32 MaxInventory = 10;
};
