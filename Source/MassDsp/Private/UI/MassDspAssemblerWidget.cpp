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
    const FBufferEntry* Buffers,
    const FRecipeEntry* RecipeItems,
    int32 Count,
    UTextBlock* Slots[4]) const
{
    for (int32 i = 0; i < 4; ++i)
    {
        UTextBlock* SlotText = Slots[i];
        if (!SlotText) continue;

        if (i < Count)
        {
            // 优先使用配方中的物品类型（当缓冲区还未入货时也能显示正确物品名）
            const EItemType DisplayType =
                (RecipeItems && RecipeItems[i].ItemType != EItemType::None)
                    ? RecipeItems[i].ItemType
                    : Buffers[i].ItemType;

            if (DisplayType != EItemType::None)
            {
                const int32 CurAmount = Buffers[i].Amount;
                const int32 ReqAmount = RecipeItems ? RecipeItems[i].Amount : 0;

                FString SlotStr;
                if (ReqAmount > 0)
                    SlotStr = FString::Printf(TEXT("%s  %d / %d"),
                        *GetItemTypeDisplayName(DisplayType).ToString(), CurAmount, ReqAmount);
                else
                    SlotStr = FString::Printf(TEXT("%s  %d"),
                        *GetItemTypeDisplayName(DisplayType).ToString(), CurAmount);

                SlotText->SetText(FText::FromString(SlotStr));
                SlotText->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
            else
            {
                // 配方未配置此槽
                SlotText->SetText(FText::FromString(TEXT("—")));
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
        TextBlock_Speed->SetText(FText::FromString(
            FString::Printf(TEXT("×%.2f"), F->CraftingSpeedMultiplier)));
    }

    // 输入缓冲区槽（传入配方输入条目以评断物品类型和需求量）
    UTextBlock* InputSlots[4] = {
        TextBlock_Input_0, TextBlock_Input_1,
        TextBlock_Input_2, TextBlock_Input_3
    };
    RefreshBufferSlots(F->InputBuffers, F->CurrentRecipe.Inputs, F->CurrentRecipe.InputsCount, InputSlots);

    // 输出缓冲区槽
    UTextBlock* OutputSlots[4] = {
        TextBlock_Output_0, TextBlock_Output_1,
        TextBlock_Output_2, TextBlock_Output_3
    };
    RefreshBufferSlots(F->OutputBuffers, F->CurrentRecipe.Outputs, F->CurrentRecipe.OutputsCount, OutputSlots);
}
