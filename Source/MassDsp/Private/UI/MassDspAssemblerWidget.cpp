#include "UI/MassDspAssemblerWidget.h"

#include "MassEntitySubsystem.h"
#include "Components/Button.h"
#include "Fragments/MassDspAssemblerFragment.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Subsystems/MassDspManager.h"

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

EItemType UMassDspAssemblerWidget::GetSuggestedTransferItemType() const
{
    const FMassDspAssemblerFragment* Fragment = GetFragment();
    UGameConfigData* GameConfig = GetGameConfig();
    if (!Fragment || !GameConfig) return EItemType::None;

    const FRecipeConfigData* RecipeConfig = GameConfig->GetRecipeConfig(Fragment->ActiveRecipeType);
    if (!RecipeConfig) return EItemType::None;

    if (!RecipeConfig->Outputs.IsEmpty())
    {
        return RecipeConfig->Outputs[0].ItemType;
    }
    if (!RecipeConfig->Inputs.IsEmpty())
    {
        return RecipeConfig->Inputs[0].ItemType;
    }
    return EItemType::None;
}

void UMassDspAssemblerWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Button_PrevRecipe && !Button_PrevRecipe->OnClicked.IsBound())
    {
        Button_PrevRecipe->OnClicked.AddDynamic(this, &UMassDspAssemblerWidget::OnPrevRecipeClicked);
    }

    if (Button_NextRecipe && !Button_NextRecipe->OnClicked.IsBound())
    {
        Button_NextRecipe->OnClicked.AddDynamic(this, &UMassDspAssemblerWidget::OnNextRecipeClicked);
    }
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

    UGameConfigData* GameConfig = GetGameConfig();
    if (!GameConfig) return;
    const FRecipeConfigData* RecipeConfig = GameConfig->GetRecipeConfig(F->ActiveRecipeType);
    if (!RecipeConfig) return;
    const FRecipeDataForFragment Recipe = RecipeConfig->ToFragment(F->ActiveRecipeType);

    // 合成进度条（由绝对时间戳反推 [0,1]）
    const float WidgetWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    ProgressBar_Crafting->SetPercent(F->GetCraftingProgress(WidgetWorldTime, Recipe));

    // 配方名称
    TextBlock_RecipeType->SetText(GetRecipeTypeDisplayName(F->ActiveRecipeType));

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
    RefreshBufferSlots(F->InputBuffers, Recipe.Inputs, Recipe.InputsCount, InputSlots);

    // 输出缓冲区槽
    UTextBlock* OutputSlots[4] = {
        TextBlock_Output_0, TextBlock_Output_1,
        TextBlock_Output_2, TextBlock_Output_3
    };
    RefreshBufferSlots(F->OutputBuffers, Recipe.Outputs, Recipe.OutputsCount, OutputSlots);
}

void UMassDspAssemblerWidget::ChangeRecipe(int32 Direction)
{
    const FMassDspAssemblerFragment* Fragment = GetFragment();
    UMassDspManager* Manager = GetDspManager();
    UGameConfigData* GameConfig = GetGameConfig();
    if (!Fragment || !Manager || !GameConfig) return;

    TArray<ERecipeType> Recipes;
    const UEnum* Enum = StaticEnum<ERecipeType>();
    if (!Enum) return;

    for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
    {
        const ERecipeType RecipeType = static_cast<ERecipeType>(Enum->GetValueByIndex(Index));
        if (RecipeType == ERecipeType::None) continue;
        if (GameConfig->GetRecipeConfig(RecipeType))
        {
            Recipes.Add(RecipeType);
        }
    }

    if (Recipes.IsEmpty()) return;

    int32 CurrentIndex = Recipes.Find(Fragment->ActiveRecipeType);
    if (CurrentIndex == INDEX_NONE) CurrentIndex = 0;

    const int32 NewIndex = (CurrentIndex + Direction + Recipes.Num()) % Recipes.Num();
    Manager->SetAssemblerRecipe(TargetEntity, Recipes[NewIndex]);
    RefreshWidgets();
}

void UMassDspAssemblerWidget::OnPrevRecipeClicked()
{
    ChangeRecipe(-1);
}

void UMassDspAssemblerWidget::OnNextRecipeClicked()
{
    ChangeRecipe(1);
}
