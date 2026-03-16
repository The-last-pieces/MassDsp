#include "Fragments/MassDspAssemblerFragment.h"

EItemType FMassDspAssemblerFragment::TryProvideItemToSlot(int SlotIdx, const FRecipeDataForFragment& Recipe)
{
    auto& Entry = OutputBuffers[SlotIdx];
    if (Entry.ItemType != EItemType::None && Entry.Amount > 0)
    {
        const EItemType Provided = Entry.ItemType;
        --Entry.Amount;
        if (Entry.Amount == 0)
        {
            Entry.ItemType = EItemType::None;
        }
        UpdateSatisfied(Recipe);
        return Provided;
    }
    return EItemType::None;
}

bool FMassDspAssemblerFragment::TryConsumeItemFromSlot(EItemType ItemType, const FRecipeDataForFragment& Recipe)
{
    for (int i = 0; i < Recipe.InputsCount; ++i)
    {
        if (Recipe.Inputs[i].ItemType == ItemType && InputBuffers[i].Amount < InputBufferCapacity)
        {
            ++InputBuffers[i].Amount;

            UpdateSatisfied(Recipe);
            return true;
        }
    }
    return false;
}

void FMassDspAssemblerFragment::TickExecute(float WorldTime, const FRecipeDataForFragment& Recipe)
{
    const float Interval = Recipe.CraftingTime / FMath::Max(CraftingSpeedMultiplier, KINDA_SMALL_NUMBER);

    // 首次就绪时初始化计时器
    if (NextCraftWorldTime <= 0.f)
    {
        NextCraftWorldTime = WorldTime + Interval;
        return;
    }

    // 输入不足或输出阻塞时暂停计时，但保留已到点状态，解除阻塞后可立即恢复。
    if (!bInputSatisfied || !bOutputSatisfied)
    {
        if (NextCraftWorldTime < WorldTime)
        {
            NextCraftWorldTime = WorldTime;
        }
        return;
    }

    if (WorldTime < NextCraftWorldTime) return;

    // 计算本帧应批量生产多少次（追帧补产）
    int32 BatchCount = FMath::FloorToInt((WorldTime - NextCraftWorldTime) / Interval) + 1;

    // 受输入量约束
    for (int i = 0; i < Recipe.InputsCount; ++i)
        BatchCount = FMath::Min(BatchCount, InputBuffers[i].Amount / Recipe.Inputs[i].Amount);

    // 受输出缓冲容量约束
    for (int i = 0; i < Recipe.OutputsCount; ++i)
    {
        auto& Out = OutputBuffers[i];
        if (Out.ItemType != EItemType::None && Out.ItemType != Recipe.Outputs[i].ItemType)
        {
            BatchCount = 0;
            break;
        }
        BatchCount = FMath::Min(BatchCount, (OutputBufferCapacity - Out.Amount) / Recipe.Outputs[i].Amount);
    }

    if (BatchCount > 0)
    {
        for (int i = 0; i < Recipe.InputsCount; ++i)
            InputBuffers[i].Amount -= BatchCount * Recipe.Inputs[i].Amount;

        for (int i = 0; i < Recipe.OutputsCount; ++i)
        {
            if (OutputBuffers[i].ItemType == EItemType::None)
                OutputBuffers[i].ItemType = Recipe.Outputs[i].ItemType;
            OutputBuffers[i].Amount += BatchCount * Recipe.Outputs[i].Amount;
        }
        NextCraftWorldTime += BatchCount * Interval;
        if (NextCraftWorldTime < WorldTime)
        {
            NextCraftWorldTime = WorldTime;
        }
        UpdateSatisfied(Recipe);
    }
    else
    {
        // 到点但当前无法执行时，保持在“已就绪”状态，解除阻塞后立即恢复。
        NextCraftWorldTime = WorldTime;
    }
}

bool FMassDspAssemblerFragment::IsRunning() const
{
    return bInputSatisfied && bOutputSatisfied;
}

void FMassDspAssemblerFragment::UpdateSatisfied(const FRecipeDataForFragment& Recipe)
{
    bool bAllMet = true;
    for (int j = 0; j < Recipe.InputsCount; ++j)
    {
        if (InputBuffers[j].Amount < Recipe.Inputs[j].Amount)
        {
            bAllMet = false;
            break;
        }
    }
    bInputSatisfied = bAllMet;
    bAllMet = true;
    for (int j = 0; j < Recipe.OutputsCount; ++j)
    {
        if (OutputBuffers[j].Amount + Recipe.Outputs[j].Amount > OutputBufferCapacity)
        {
            bAllMet = false;
            break;
        }
    }
    bOutputSatisfied = bAllMet;
}
