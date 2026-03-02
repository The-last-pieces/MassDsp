#include "UI/MassDspAssemblerWidget.h"

#include "MassEntitySubsystem.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

// 

const FMassDspAssemblerFragment* UMassDspAssemblerWidget::GetFragment() const
{
    if (!TargetEntity.IsValid()) return nullptr;
    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return nullptr;
    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(TargetEntity)) return nullptr;
    return EM.GetFragmentDataPtr<FMassDspAssemblerFragment>(TargetEntity);
}

// 

void UMassDspAssemblerWidget::RefreshBufferSlots(
    const FBufferEntry* Buffers, int32 Count,
    UTextBlock* Slots[4]) const
{
    for (int32 i = 0; i < 4; ++i)
    {
        UTextBlock* SlotText = Slots[i];
        if (!SlotText) continue;

        if (i < Count)
        {
            const FBufferEntry& Entry = Buffers[i];
            if (Entry.ItemType != EItemType::None)
            {
                SlotText->SetText(FText::Format(
                    NSLOCTEXT("MassDsp", "AssemblerBufferSlot", "{0} {1}"),
                    GetItemTypeDisplayName(Entry.ItemType),
                    FText::AsNumber(Entry.Amount)));
                SlotText->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
            else
            {
                SlotText->SetText(FText::FromString(TEXT("")));
                SlotText->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
        }
        else
        {
            // 此槽位超出配方数量，隐藏
            SlotText->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}

// 

void UMassDspAssemblerWidget::RefreshWidgets()
{
    const FMassDspAssemblerFragment* F = GetFragment();
    if (!F) return;

    // 合成进度条
    ProgressBar_Crafting->SetPercent(F->CraftingProgress);

    // 配方名称
    TextBlock_RecipeType->SetText(GetRecipeTypeDisplayName(F->CurrentRecipe.RecipeType));

    // 速度倍率（可选）
    if (TextBlock_Speed)
    {
        TextBlock_Speed->SetText(FText::Format(
            NSLOCTEXT("MassDsp", "AssemblerSpeed", "速度 {0}"),
            FText::AsNumber(F->CraftingSpeedMultiplier)));
    }

    // 输入缓冲区槽
    UTextBlock* InputSlots[4] = {
        TextBlock_Input_0, TextBlock_Input_1,
        TextBlock_Input_2, TextBlock_Input_3
    };
    RefreshBufferSlots(F->InputBuffers, F->CurrentRecipe.InputsCount, InputSlots);

    // 输出缓冲区槽
    UTextBlock* OutputSlots[4] = {
        TextBlock_Output_0, TextBlock_Output_1,
        TextBlock_Output_2, TextBlock_Output_3
    };
    RefreshBufferSlots(F->OutputBuffers, F->CurrentRecipe.OutputsCount, OutputSlots);
}
