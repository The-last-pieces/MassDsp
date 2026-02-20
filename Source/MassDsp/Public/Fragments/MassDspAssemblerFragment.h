#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "MassDspAssemblerFragment.generated.h"

USTRUCT()
struct FBufferEntry
{
    GENERATED_BODY()

    UPROPERTY()
    EItemType ItemType = EItemType::None;

    UPROPERTY()
    int32 Amount = 0;

    FBufferEntry() = default;
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

    UPROPERTY()
    int32 InputBufferCapacity = 10;

    UPROPERTY()
    int32 OutputBufferCapacity = 10;

    UPROPERTY()
    FBufferEntry InputBuffers[FGameConst::SlotMaxCount - 1];

    UPROPERTY()
    FBufferEntry OutputBuffers[FGameConst::SlotMaxCount - 1];

    EItemType TryProvideItemToSlot(int SlotIdx);

    bool TryConsumeItemFromSlot(EItemType ItemType);

    void TickExecute(float DeltaTime);
};
