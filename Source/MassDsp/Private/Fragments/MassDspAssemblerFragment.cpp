#include "Fragments/MassDspAssemblerFragment.h"

EItemType FMassDspAssemblerFragment::TryProvideItemToSlot(int SlotIdx)
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

            // 检查是否所有输入槽都已达到配方需求量，更新缓存标记
            // 循环体最多 4 次，仅在入库时触发一次，不在每帧 TickExecute 里重复
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
            return true;
        }
    }
    return false;
}

void FMassDspAssemblerFragment::TickExecute(float WorldTime, const FRecipeDataForFragment& Recipe)
{
    // 输入未满足：一个 bool 判断立即返回，零额外开销
    if (!bInputSatisfied) return;

    if (WorldTime < NextCraftWorldTime) return;

    const float Interval = Recipe.CraftingTime / FMath::Max(CraftingSpeedMultiplier, KINDA_SMALL_NUMBER);

    // 首次就绪时初始化计时器
    if (NextCraftWorldTime <= 0.f)
    {
        NextCraftWorldTime = WorldTime + Interval;
        return;
    }

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

        // 输入已消耗，清除满足标记并重置计时器，等待下次补充
        bInputSatisfied = false;
        NextCraftWorldTime = 0.f;
    }
    else
    {
        // 输出满导致阻塞：推进计时器避免下帧空转
        NextCraftWorldTime = WorldTime + Interval;
    }
}
