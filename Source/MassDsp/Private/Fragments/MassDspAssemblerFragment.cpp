#include "Fragments/MassDspAssemblerFragment.h"

EItemType FMassDspAssemblerFragment::TryProvideItemToSlot(int SlotIdx)
{
    if (auto& Entry = OutputBuffers[SlotIdx]; Entry.ItemType != EItemType::None && Entry.Amount > 0)
    {
        --Entry.Amount;
        return Entry.ItemType;
    }
    return EItemType::None;
}

bool FMassDspAssemblerFragment::TryConsumeItemFromSlot(EItemType ItemType)
{
    for (int i = 0; i < FGameConst::SlotMaxCount - 1; ++i)
    {
        if (auto& Entry = InputBuffers[i]; CurrentRecipe.Inputs[i].ItemType == ItemType && Entry.Amount < InputBufferCapacity)
        {
            ++Entry.Amount;
            return true;
        }
    }
    return false;
}

void FMassDspAssemblerFragment::TickExecute(float DeltaTime)
{
    if (CurrentRecipe.RecipeType == ERecipeType::None) return;

    // 检查输入是否满足配方要求才加进度条

    for (int i = 0; i < CurrentRecipe.InputsCount; ++i)
    {
        if (InputBuffers[i].Amount < CurrentRecipe.Inputs[i].Amount)
        {
            return;
        }
    }

    CraftingProgress += DeltaTime * CraftingSpeedMultiplier / CurrentRecipe.CraftingTime;

    if (auto ProductCount = FMath::FloorToInt(CraftingProgress); ProductCount > 0)
    {
        // 取最小可能值, 遍历每种输入, 计算实际能生成的数量
        for (int i = 0; i < CurrentRecipe.InputsCount; ++i)
        {
            auto PossibleCount = InputBuffers[i].Amount / CurrentRecipe.Inputs[i].Amount;
            ProductCount = FMath::Min(ProductCount, PossibleCount);
        }

        // 再取输出缓冲区剩余容量的最小值
        for (int i = 0; i < CurrentRecipe.OutputsCount; ++i)
        {
            auto& OutputEntry = OutputBuffers[i];
            if (OutputEntry.ItemType != EItemType::None && OutputEntry.ItemType != CurrentRecipe.Outputs[i].ItemType)
            {
                ProductCount = 0;
                break;
            }
            auto RemainingCapacity = OutputBufferCapacity - OutputEntry.Amount;
            ProductCount = FMath::Min(ProductCount, RemainingCapacity / CurrentRecipe.Outputs[i].Amount);
        }

        if (ProductCount > 0)
        {
            // 消耗输入
            for (int i = 0; i < CurrentRecipe.InputsCount; ++i)
            {
                InputBuffers[i].Amount -= ProductCount * CurrentRecipe.Inputs[i].Amount;
            }

            // 生产输出
            for (int i = 0; i < CurrentRecipe.OutputsCount; ++i)
            {
                auto& OutputEntry = OutputBuffers[i];
                if (OutputEntry.ItemType == EItemType::None)
                {
                    OutputEntry.ItemType = CurrentRecipe.Outputs[i].ItemType;
                }
                OutputEntry.Amount += ProductCount * CurrentRecipe.Outputs[i].Amount;
            }

            CraftingProgress -= ProductCount;
            return;
        }

        CraftingProgress = 1.0f;
    }
}
