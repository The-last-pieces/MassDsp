#pragma once

#include "CoreMinimal.h"
#include "GameConst.h"
#include "MassDspAssemblerFragment.generated.h"

/**
 * 配方数据 Shared Fragment：同配方类型的所有合成台共享同一份内存块。
 * 将配方数据（~80B）从每个实体移出，使 FMassDspAssemblerFragment 体积减半，
 * Chunk 内每个 Cache Line 能克入更多实体。
 */
USTRUCT()
struct MASSDSP_API FMassDspRecipeSharedFragment : public FMassSharedFragment
{
    GENERATED_BODY()

    UPROPERTY()
    FRecipeDataForFragment Recipe;

    bool operator==(const FMassDspRecipeSharedFragment& Other) const
    {
        return Recipe.RecipeType == Other.Recipe.RecipeType;
    }

    friend uint32 GetTypeHash(const FMassDspRecipeSharedFragment& Fragment)
    {
        return GetTypeHash(Fragment.Recipe.RecipeType);
    }
};

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
 * 合成台特定的 Fragment
 * 包含合成台的进度和缓冲区数据（配方数据已移至 FMassDspRecipeSharedFragment）
 */
USTRUCT()
struct MASSDSP_API FMassDspAssemblerFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY()
    ERecipeType ActiveRecipeType = ERecipeType::None;

    // 下次合成触发的绝对世界时间（0 = 未初始化）
    UPROPERTY()
    float NextCraftWorldTime = 0.0f;

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

    // 所有输入缓冲是否满足配方需求量的缓存标记。
    // 仅在 TryConsumeItemFromSlot 填入饱和时置 true，消耗输入后清 false。
    // TickExecute 入口直接 return，不再每帧遍历 InputBuffers 作比较。
    bool bInputSatisfied = false;
    bool bOutputSatisfied = true;

    EItemType TryProvideItemToSlot(int SlotIdx, const FRecipeDataForFragment& Recipe);

    // 入库成功后自动更新 bInputSatisfied
    bool TryConsumeItemFromSlot(EItemType ItemType, const FRecipeDataForFragment& Recipe);

    // WorldTime = World->GetTimeSeconds()，绝大多数帧一个 bool 判断即返回
    void TickExecute(float WorldTime, const FRecipeDataForFragment& Recipe);

    // 返回 [0,1] 进度供 UI 使用（WorldTime = World->GetTimeSeconds()）
    float GetCraftingProgress(float WorldTime, const FRecipeDataForFragment& Recipe) const
    {
        if (!bInputSatisfied || NextCraftWorldTime <= 0.f) return 0.f;
        const float Interval = Recipe.CraftingTime / FMath::Max(CraftingSpeedMultiplier, KINDA_SMALL_NUMBER);
        return FMath::Clamp(1.f - (NextCraftWorldTime - WorldTime) / Interval, 0.f, 1.f);
    }

    bool IsRunning() const;

    float GetCraftInterval(const FRecipeDataForFragment& Recipe) const
    {
        return Recipe.CraftingTime / FMath::Max(CraftingSpeedMultiplier, KINDA_SMALL_NUMBER);
    }

    bool CanSustainTheoreticalRate(const FRecipeDataForFragment& Recipe) const
    {
        return ActiveRecipeType != ERecipeType::None
            && Recipe.CraftingTime > KINDA_SMALL_NUMBER
            && bInputSatisfied
            && bOutputSatisfied;
    }

    float GetTheoreticalInputRate(const FRecipeDataForFragment& Recipe, int32 InputIndex) const
    {
        if (!CanSustainTheoreticalRate(Recipe) || Recipe.InputsCount <= InputIndex)
        {
            return 0.f;
        }
        return static_cast<float>(Recipe.Inputs[InputIndex].Amount) / GetCraftInterval(Recipe);
    }

    float GetTheoreticalOutputRate(const FRecipeDataForFragment& Recipe, int32 OutputIndex) const
    {
        if (!CanSustainTheoreticalRate(Recipe) || Recipe.OutputsCount <= OutputIndex)
        {
            return 0.f;
        }
        return static_cast<float>(Recipe.Outputs[OutputIndex].Amount) / GetCraftInterval(Recipe);
    }

    void UpdateSatisfied(const FRecipeDataForFragment& Recipe);

    void ResetForRecipeChange()
    {
        NextCraftWorldTime = 0.f;
        bInputSatisfied = false;
        bOutputSatisfied = true;

        for (FBufferEntry& Entry : InputBuffers)
        {
            Entry.ItemType = EItemType::None;
            Entry.Amount = 0;
        }

        for (FBufferEntry& Entry : OutputBuffers)
        {
            Entry.ItemType = EItemType::None;
            Entry.Amount = 0;
        }
    }
};
