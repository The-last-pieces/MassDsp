#include "UI/MassDspAssemblerWidget.h"

#include "MassEntitySubsystem.h"
#include "Fragments/MassDspAssemblerFragment.h"
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
    if (!TargetEntity.IsValid()) return;
    UMassEntitySubsystem* ESub = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    if (!ESub) return;
    FMassEntityManager& EM = ESub->GetMutableEntityManager();
    if (!EM.IsEntityValid(TargetEntity)) return;

    const FMassDspAssemblerFragment* F = EM.GetFragmentDataPtr<FMassDspAssemblerFragment>(TargetEntity);
    if (!F) return;

    // 从 SharedFragment 读配方（不依赖 Fragment 内的任何指针字段）
    auto Recipe = &EM.GetSharedFragmentDataChecked<FMassDspRecipeSharedFragment>(TargetEntity).Recipe;

    // 合成进度条（由绝对时间戳反推 [0,1]）
    const float WidgetWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    ProgressBar_Crafting->SetPercent(F->GetCraftingProgress(WidgetWorldTime, *Recipe));

    // 配方名称
    TextBlock_RecipeType->SetText(GetRecipeTypeDisplayName(Recipe->RecipeType));

    // 速度倍率（可选）
    if (TextBlock_Speed)
    {
        TextBlock_Speed->SetText(FText::FromString(
            FString::Printf(TEXT("×%.2f"), F->CraftingSpeedMultiplier)));
    }

    // 输入缓冲区槽
    UTextBlock* InputSlots[4] = {
        TextBlock_Input_0, TextBlock_Input_1,
        TextBlock_Input_2, TextBlock_Input_3
    };
    if (Recipe)
    {
        RefreshBufferSlots(F->InputBuffers, Recipe->Inputs, Recipe->InputsCount, InputSlots);
    }

    // 输出缓冲区槽
    UTextBlock* OutputSlots[4] = {
        TextBlock_Output_0, TextBlock_Output_1,
        TextBlock_Output_2, TextBlock_Output_3
    };
    if (Recipe)
    {
        RefreshBufferSlots(F->OutputBuffers, Recipe->Outputs, Recipe->OutputsCount, OutputSlots);
    }
}
